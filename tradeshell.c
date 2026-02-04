/*
  tradeshell.c - Oracle Linux AutoTrade Dedicated Shell (Fixed Full)

  Builtins:
    help, exit,
    start, stop, restart, status, health,
    log, config, backup, restore,
    nano, ls, cat, scat, grep

  Notes:
    - Arguments are split by whitespace only (no quotes, no pipes, no redirects).
    - sudo is auto-detected:
        if `sudo -n true` returns 0, systemctl uses sudo.
      scat always uses sudo.

  Build:
    gcc -O2 -Wall -Wextra -o tradeshell tradeshell.c

  Optional readline (history):
    sudo dnf install -y readline-devel
    gcc -O2 -Wall -Wextra -DUSE_READLINE -o tradeshell tradeshell.c -lreadline
*/

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

// ====== fixed commands / paths ======
static const char *SERVICE_NAME = "fx-autotrade";

static const char *SYSTEMCTL = "systemctl";
static const char *PYTHON3   = "python3";
static const char *NANO      = "nano";
static const char *LS        = "ls";
static const char *CAT       = "cat";
static const char *GREP      = "grep";

static const char *LOG_TOOL     = "/opt/tools/get_log.py";
static const char *CONFIG_TOOL  = "/opt/tools/xmledit.py";
static const char *BACKUP_TOOL  = "/opt/Innovations/tools/Buckup.py";
static const char *RESTORE_TOOL = "/opt/Innovations/tools/Restore.py";

static const char *SUDO = "sudo";
static int g_use_sudo = 0;
// ===================================

// ====== helpers ======
static int run_cmd_capture_rc(char *const argv[]);
static void detect_sudo(void);
static void print_usage(void);

static void build_passthrough_argv(char **args,
                                   const char *prefix0,
                                   const char *prefix1,
                                   char ***out_argv)
{
  int count = 0;
  while (args[count] != NULL) count++;

  int prefix_count = (prefix0 ? 1 : 0) + (prefix1 ? 1 : 0);
  int total = prefix_count + (count - 1) + 1; // + NULL

  char **argv = calloc((size_t)total, sizeof(char*));
  if (!argv) {
    perror("trade: calloc");
    *out_argv = NULL;
    return;
  }

  int i = 0;
  if (prefix0) argv[i++] = (char*)prefix0;
  if (prefix1) argv[i++] = (char*)prefix1;
  for (int j = 1; j < count; j++) argv[i++] = args[j];
  argv[i] = NULL;

  *out_argv = argv;
}

static int run_cmd_capture_rc(char *const argv[])
{
  pid_t pid = fork();
  if (pid == 0) {
    execvp(argv[0], argv);
    fprintf(stderr, "trade: execvp failed: %s (%s)\n", argv[0], strerror(errno));
    _exit(127);
  } else if (pid < 0) {
    perror("trade: fork");
    return 1;
  } else {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
      perror("trade: waitpid");
      return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
  }
}

static void detect_sudo(void)
{
  // sudo -n true => 0 if non-interactive sudo allowed (NOPASSWD or cached)
  char *const argv[] = {(char*)SUDO, "-n", "true", NULL};
  int rc = run_cmd_capture_rc(argv);
  g_use_sudo = (rc == 0);
}

static void print_usage(void)
{
  puts("AutoTrade Shell (Oracle Linux)");
  puts("Commands:");
  puts("  start                 [sudo] systemctl start fx-autotrade");
  puts("  stop                  [sudo] systemctl stop fx-autotrade");
  puts("  restart               [sudo] systemctl restart fx-autotrade");
  puts("  status                [sudo] systemctl status fx-autotrade");
  puts("  health                service + log + disk + mem + time");
  puts("  log [ARGS...]         python3 /opt/tools/get_log.py [ARGS...]");
  puts("  config [ARGS...]      python3 /opt/tools/xmledit.py [ARGS...]");
  puts("  backup [ARGS...]      python3 /opt/Innovations/tools/Buckup.py [ARGS...]");
  puts("  restore [ARGS...]     python3 /opt/Innovations/tools/Restore.py [ARGS...]");
  puts("  nano [ARGS...]        nano [ARGS...]");
  puts("  ls [ARGS...]          ls [ARGS...]");
  puts("  cat [ARGS...]         cat [ARGS...]");
  puts("  scat [ARGS...]        sudo cat [ARGS...]");
  puts("  grep [ARGS...]        grep [ARGS...]");
  puts("  help                  show this help");
  puts("  exit                  quit");
  puts("");
  puts("Notes:");
  puts("  - Arguments are split by whitespace only (no quotes/pipes/redirects).");
  puts("  - sudo is auto-detected (sudo -n true). systemctl uses sudo when available.");
}

// ====== builtins declarations ======
static int sh_help(char **args);
static int sh_exit(char **args);
static int sh_start(char **args);
static int sh_stop(char **args);
static int sh_restart(char **args);
static int sh_status(char **args);
static int sh_health(char **args);
static int sh_log(char **args);
static int sh_config(char **args);
static int sh_backup(char **args);
static int sh_restore(char **args);
static int sh_nano(char **args);
static int sh_ls(char **args);
static int sh_cat(char **args);
static int sh_scat(char **args);
static int sh_grep(char **args);

// ====== builtin tables (REAL definitions, no redeclare later) ======
static char *builtin_str[] = {
  "help",
  "exit",
  "start",
  "stop",
  "restart",
  "status",
  "health",
  "log",
  "config",
  "backup",
  "restore",
  "nano",
  "ls",
  "cat",
  "scat",
  "grep",
};

static int (*builtin_func[])(char **) = {
  &sh_help,
  &sh_exit,
  &sh_start,
  &sh_stop,
  &sh_restart,
  &sh_status,
  &sh_health,
  &sh_log,
  &sh_config,
  &sh_backup,
  &sh_restore,
  &sh_nano,
  &sh_ls,
  &sh_cat,
  &sh_scat,
  &sh_grep,
};

static int num_builtins(void)
{
  return (int)(sizeof(builtin_str) / sizeof(builtin_str[0]));
}

// ====== builtins implementations ======
static int sh_help(char **args) { (void)args; print_usage(); return 1; }
static int sh_exit(char **args) { (void)args; return 0; }

static int sh_start(char **args)
{
  (void)args;
  int rc;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "start", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "start", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  }
  if (rc == 0) puts("trade: started.");
  else fprintf(stderr, "trade: start failed (rc=%d)\n", rc);
  return 1;
}

static int sh_stop(char **args)
{
  (void)args;
  int rc;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "stop", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "stop", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  }
  if (rc == 0) puts("trade: stopped.");
  else fprintf(stderr, "trade: stop failed (rc=%d)\n", rc);
  return 1;
}

static int sh_restart(char **args)
{
  (void)args;
  int rc;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "restart", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "restart", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  }
  if (rc == 0) puts("trade: restarted.");
  else fprintf(stderr, "trade: restart failed (rc=%d)\n", rc);
  return 1;
}

static int sh_status(char **args)
{
  (void)args;
  int rc;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "status", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "status", (char*)SERVICE_NAME, NULL};
    rc = run_cmd_capture_rc(argv);
  }
  if (rc != 0) fprintf(stderr, "trade: status returned rc=%d\n", rc);
  return 1;
}

static int sh_health(char **args)
{
  (void)args;
  puts("=== HEALTH CHECK ===");

  puts("[1/5] service status");
  (void)sh_status(NULL);

  puts("\n[2/5] bot logs");
  char *const lg[] = {(char*)PYTHON3, (char*)LOG_TOOL, NULL};
  (void)run_cmd_capture_rc(lg);

  puts("\n[3/5] disk (df -h /)");
  char *const df[] = {"df", "-h", "/", NULL};
  (void)run_cmd_capture_rc(df);

  puts("\n[4/5] memory (free -h)");
  char *const fr[] = {"free", "-h", NULL};
  (void)run_cmd_capture_rc(fr);

  puts("\n[5/5] time (date)");
  char *const dt[] = {"date", NULL};
  (void)run_cmd_capture_rc(dt);

  puts("\n=== END HEALTH ===");
  return 1;
}

static int sh_log(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, PYTHON3, LOG_TOOL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: log failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_config(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, PYTHON3, CONFIG_TOOL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: config failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_backup(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, PYTHON3, BACKUP_TOOL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: backup failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_restore(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, PYTHON3, RESTORE_TOOL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: restore failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_nano(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, NANO, NULL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: nano failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_ls(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, LS, NULL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: ls failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_cat(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, CAT, NULL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: cat failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_scat(char **args)
{
  // scat [ARGS...] -> sudo cat [ARGS...]
  int count = 0;
  while (args[count] != NULL) count++;

  char **argv = calloc((size_t)count + 2, sizeof(char*));
  if (!argv) { perror("trade: calloc"); return 1; }

  int i = 0;
  argv[i++] = (char*)SUDO;
  argv[i++] = (char*)CAT;
  for (int j = 1; j < count; j++) argv[i++] = args[j];
  argv[i] = NULL;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: scat failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

static int sh_grep(char **args)
{
  char **argv = NULL;
  build_passthrough_argv(args, GREP, NULL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: grep failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

// ====== core loop ======
#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n"

static char **split_line(char *line)
{
  int bufsize = TOK_BUFSIZE, position = 0;
  char **tokens = malloc(bufsize * sizeof(char*));
  if (!tokens) { perror("trade: malloc"); exit(1); }

  char *token = strtok(line, TOK_DELIM);
  while (token != NULL) {
    tokens[position++] = token;
    if (position >= bufsize) {
      bufsize += TOK_BUFSIZE;
      char **tmp = realloc(tokens, bufsize * sizeof(char*));
      if (!tmp) { free(tokens); perror("trade: realloc"); exit(1); }
      tokens = tmp;
    }
    token = strtok(NULL, TOK_DELIM);
  }
  tokens[position] = NULL;
  return tokens;
}

static int execute(char **args)
{
  if (args[0] == NULL) return 1;

  for (int i = 0; i < num_builtins(); i++) {
    if (strcmp(args[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(args);
    }
  }

  fprintf(stderr, "trade: unknown command: %s (type 'help')\n", args[0]);
  return 1;
}

static char *read_line(void)
{
#ifdef USE_READLINE
  char *line = readline("trade> ");
  if (!line) exit(0);
  if (*line) add_history(line);
  return line;
#else
  char *line = NULL;
  size_t cap = 0;
  printf("trade> ");
  fflush(stdout);
  ssize_t n = getline(&line, &cap, stdin);
  if (n < 0) exit(0);
  return line;
#endif
}

static void loop(void)
{
  int status = 1;
  while (status) {
    char *line = read_line();
    char **args = split_line(line);
    status = execute(args);
    free(args);
    free(line);
  }
}

int main(void)
{
  detect_sudo();
  printf("AutoTrade Shell (trade)  sudo=%s  type 'help'\n", g_use_sudo ? "on" : "off");
  loop();
  return 0;
}
