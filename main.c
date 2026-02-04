/*
  tradeshell.c - Oracle Linux AutoTrade Dedicated Shell

  Mappings (from your table):
    - start/stop/restart/status : systemctl fx-autotrade (often via sudo)
    - log                       : python3 /opt/tools/get_log.py
    - config                    : python3 /opt/tools/xmledit.py   (internal-complete; args passthrough)
    - backup                    : python3 /opt/Innovations/tools/Buckup.py
    - restore                   : python3 /opt/Innovations/tools/Restore.py
    - nano                      : standard nano (args passthrough)

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
#include <ctype.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

// ====== fixed paths / commands ======
static const char *SERVICE_NAME = "fx-autotrade";

static const char *SYSTEMCTL = "systemctl";
static const char *PYTHON3   = "python3";
static const char *NANO      = "nano";

static const char *LOG_TOOL     = "/opt/tools/get_log.py";
static const char *CONFIG_TOOL  = "/opt/tools/xmledit.py";
static const char *BACKUP_TOOL  = "/opt/Innovations/tools/Buckup.py";
static const char *RESTORE_TOOL = "/opt/Innovations/tools/Restore.py";

// sudo optional: auto-detect
static int g_use_sudo = 0;
static const char *SUDO = "sudo";
// ===================================

static int run_cmd(char *const argv[]);
static int run_cmd_capture_rc(char *const argv[]);
static void print_usage(void);
static void detect_sudo(void);

// ---- builtins ----
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
};

static int (*builtin_func[]) (char **) = {
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
};

static int num_builtins(void) {
  return (int)(sizeof(builtin_str) / sizeof(builtin_str[0]));
}

// ====== command runner ======
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

static int run_cmd(char *const argv[])
{
  (void)run_cmd_capture_rc(argv);
  return 1;
}

static void print_usage(void)
{
  puts("AutoTrade Shell (Oracle Linux)");
  puts("Commands:");
  puts("  start                 [sudo] systemctl start fx-autotrade");
  puts("  stop                  [sudo] systemctl stop fx-autotrade");
  puts("  restart               [sudo] systemctl restart fx-autotrade");
  puts("  status                [sudo] systemctl status fx-autotrade");
  puts("  health                bundle check: service + log + disk + mem + time");
  puts("  log [ARGS...]         python3 /opt/tools/get_log.py [ARGS...]");
  puts("  config [ARGS...]      python3 /opt/tools/xmledit.py [ARGS...]");
  puts("  backup [ARGS...]      python3 /opt/Innovations/tools/Buckup.py [ARGS...]");
  puts("  restore [ARGS...]     python3 /opt/Innovations/tools/Restore.py [ARGS...]");
  puts("  nano [ARGS...]        nano [ARGS...]");
  puts("  help                  show this help");
  puts("  exit                  quit");
  puts("");
  puts("Notes:");
  puts("  - Arguments are split by whitespace. Quotes are NOT supported.");
  puts("  - sudo is auto-detected. If 'sudo -n true' works, systemctl uses sudo.");
}

static void detect_sudo(void)
{
  // If sudo exists and can run non-interactively, prefer it.
  // sudo -n true  => exit 0 if NOPASSWD or cached; otherwise non-zero.
  char *const argv[] = {(char*)SUDO, "-n", "true", NULL};
  int rc = run_cmd_capture_rc(argv);
  g_use_sudo = (rc == 0);
}

static void build_passthrough_argv(char **args, const char *prefix0, const char *prefix1, char ***out_argv)
{
  // args[0] is builtin name, pass args[1..]
  int count = 0;
  while (args[count] != NULL) count++;

  // prefix0 + prefix1 + (count-1 args) + NULL
  // prefix1 may be NULL if only one prefix desired
  int prefix_count = (prefix0 ? 1 : 0) + (prefix1 ? 1 : 0);
  int total = prefix_count + (count - 1) + 1;

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

// ====== builtins ======
static int sh_help(char **args) { (void)args; print_usage(); return 1; }
static int sh_exit(char **args) { (void)args; return 0; }

static int sh_start(char **args)
{
  (void)args;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "start", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: started.");
    else fprintf(stderr, "trade: start failed (rc=%d)\n", rc);
    return 1;
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "start", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: started.");
    else fprintf(stderr, "trade: start failed (rc=%d)\n", rc);
    return 1;
  }
}

static int sh_stop(char **args)
{
  (void)args;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "stop", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: stopped.");
    else fprintf(stderr, "trade: stop failed (rc=%d)\n", rc);
    return 1;
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "stop", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: stopped.");
    else fprintf(stderr, "trade: stop failed (rc=%d)\n", rc);
    return 1;
  }
}

static int sh_restart(char **args)
{
  (void)args;
  if (g_use_sudo) {
    char *const argv[] = {(char*)SUDO, (char*)SYSTEMCTL, "restart", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: restarted.");
    else fprintf(stderr, "trade: restart failed (rc=%d)\n", rc);
    return 1;
  } else {
    char *const argv[] = {(char*)SYSTEMCTL, "restart", (char*)SERVICE_NAME, NULL};
    int rc = run_cmd_capture_rc(argv);
    if (rc == 0) puts("trade: restarted.");
    else fprintf(stderr, "trade: restart failed (rc=%d)\n", rc);
    return 1;
  }
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
  // log [ARGS...] -> python3 LOG_TOOL [ARGS...]
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
  // config [ARGS...] -> python3 CONFIG_TOOL [ARGS...]
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
  // backup [ARGS...] -> python3 BACKUP_TOOL [ARGS...]
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
  // restore [ARGS...] -> python3 RESTORE_TOOL [ARGS...]
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
  // nano [ARGS...] -> nano [ARGS...]
  char **argv = NULL;
  build_passthrough_argv(args, NANO, NULL, &argv);
  if (!argv) return 1;

  int rc = run_cmd_capture_rc(argv);
  if (rc != 0) fprintf(stderr, "trade: nano failed (rc=%d)\n", rc);
  free(argv);
  return 1;
}

// ====== tokenizer (whitespace split; no quotes) ======
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
