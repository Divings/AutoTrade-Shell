/*
  tradeshell.c - Oracle Linux AutoTrade Dedicated Shell
  Full: HOME start + cd + pwd + quote parsing + pipes + update command.

  Builtins (parent-only, not pipe-able):
    help, exit, cd, pwd,
    start, stop, restart, status, health

  Exec-style commands (pipe-able):
    log, config, backup, restore,
    nano, ls, cat, scat, grep,
    update

  Notes:
    - Quote support: "..." and '...'
      - Backslash escapes are handled in unquoted and double-quoted strings.
      - Single quotes take everything literally until next '.
    - Pipe support: cmd1 | cmd2 | ...
      - Only exec-style commands are allowed in pipelines.
    - On startup, chdir(HOME) if HOME is set.
    - sudo auto-detect: if `sudo -n true` works, use sudo for systemctl.
      - scat always uses sudo cat
      - update uses sudo when available; otherwise tries without sudo.

  Build:
    gcc -O2 -Wall -Wextra -o tradeshell tradeshell.c

  Optional readline:
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
static const char *BASH      = "bash";
static const char *NANO      = "nano";
static const char *LS        = "ls";
static const char *CAT       = "cat";
static const char *GREP      = "grep";

static const char *LOG_TOOL     = "/opt/tools/get_log.py";
static const char *CONFIG_TOOL  = "/opt/tools/xmledit.py";
static const char *BACKUP_TOOL  = "/opt/Innovations/tools/Buckup.py";
static const char *RESTORE_TOOL = "/opt/Innovations/tools/Restore.py";
static const char *UPDATE_TOOL  = "/opt/Innovations/System/Update.sh";

static const char *SUDO = "sudo";
static int g_use_sudo = 0;
// ===================================

// ====== helpers ======
static int run_cmd_capture_rc(char *const argv[]);
static void detect_sudo(void);
static void print_usage(void);

// ====== exec argv builder (passthrough) ======
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
  char *const argv[] = {(char*)SUDO, "-n", "true", NULL};
  int rc = run_cmd_capture_rc(argv);
  g_use_sudo = (rc == 0);
}

static void print_usage(void)
{
  puts("AutoTrade Shell (Oracle Linux)");
  puts("Commands:");
  puts("  help                  show this help");
  puts("  exit                  quit");
  puts("  cd [DIR]              change directory (default: HOME; supports ~ and ~/...)");
  puts("  pwd                   print current directory");
  puts("");
  puts("  start                 [sudo] systemctl start fx-autotrade");
  puts("  stop                  [sudo] systemctl stop fx-autotrade");
  puts("  restart               [sudo] systemctl restart fx-autotrade");
  puts("  status                [sudo] systemctl status fx-autotrade");
  puts("  health                service + log + disk + mem + time");
  puts("");
  puts("  log [ARGS...]         python3 /opt/tools/get_log.py [ARGS...]");
  puts("  config [ARGS...]      python3 /opt/tools/xmledit.py [ARGS...]");
  puts("  backup [ARGS...]      python3 /opt/Innovations/tools/Buckup.py [ARGS...]");
  puts("  restore [ARGS...]     python3 /opt/Innovations/tools/Restore.py [ARGS...]");
  puts("  update [ARGS...]      [sudo] bash /opt/Innovations/System/Update.sh [ARGS...]");
  puts("");
  puts("  nano [ARGS...]        nano [ARGS...]");
  puts("  ls [ARGS...]          ls [ARGS...]");
  puts("  cat [ARGS...]         cat [ARGS...]");
  puts("  scat [ARGS...]        sudo cat [ARGS...]");
  puts("  grep [ARGS...]        grep [ARGS...]");
  puts("");
  puts("Pipes:");
  puts("  cat file | grep KEYWORD");
  puts("");
  puts("Quotes:");
  puts("  cat \"file name.txt\" | grep \"some word\"");
  puts("");
  puts("Notes:");
  puts("  - Only exec-style commands can be used in pipelines.");
  puts("  - systemctl uses sudo when available (sudo -n true).");
}

// ====== builtins (parent-only) ======
static int sh_help(char **args) { (void)args; print_usage(); return 1; }
static int sh_exit(char **args) { (void)args; return 0; }

static int sh_cd(char **args)
{
  const char *home = getenv("HOME");
  const char *target = NULL;

  if (!args[1] || strcmp(args[1], "~") == 0) {
    target = (home && *home) ? home : "/";
  } else if (args[1][0] == '~' && args[1][1] == '/') {
    if (!home || !*home) {
      fprintf(stderr, "trade: cd: HOME is not set\n");
      return 1;
    }
    size_t len = strlen(home) + strlen(args[1]);
    char *buf = malloc(len + 1);
    if (!buf) { perror("trade: malloc"); return 1; }
    strcpy(buf, home);
    strcat(buf, args[1] + 1); // skip '~'
    if (chdir(buf) != 0) {
      fprintf(stderr, "trade: cd: %s: %s\n", buf, strerror(errno));
    }
    free(buf);
    return 1;
  } else {
    target = args[1];
  }

  if (chdir(target) != 0) {
    fprintf(stderr, "trade: cd: %s: %s\n", target, strerror(errno));
  }
  return 1;
}

static int sh_pwd(char **args)
{
  (void)args;
  char *cwd = getcwd(NULL, 0);
  if (!cwd) {
    fprintf(stderr, "trade: pwd: %s\n", strerror(errno));
    return 1;
  }
  puts(cwd);
  free(cwd);
  return 1;
}

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

// ====== builtin tables ======
static char *builtin_str[] = {
  "help",
  "exit",
  "cd",
  "pwd",
  "start",
  "stop",
  "restart",
  "status",
  "health",
};

static int (*builtin_func[])(char **) = {
  &sh_help,
  &sh_exit,
  &sh_cd,
  &sh_pwd,
  &sh_start,
  &sh_stop,
  &sh_restart,
  &sh_status,
  &sh_health,
};

static int num_builtins(void)
{
  return (int)(sizeof(builtin_str) / sizeof(builtin_str[0]));
}

// ====== quote-aware tokenizer ======
typedef struct {
  char **items;
  int len;
  int cap;
} strvec;

static void sv_init(strvec *v) { v->items = NULL; v->len = 0; v->cap = 0; }

static void sv_push(strvec *v, char *s)
{
  if (v->len + 1 > v->cap) {
    int ncap = (v->cap == 0) ? 16 : (v->cap * 2);
    char **tmp = realloc(v->items, (size_t)ncap * sizeof(char*));
    if (!tmp) { perror("trade: realloc"); exit(1); }
    v->items = tmp;
    v->cap = ncap;
  }
  v->items[v->len++] = s;
}

static void sv_free_all(strvec *v)
{
  for (int i = 0; i < v->len; i++) free(v->items[i]);
  free(v->items);
  v->items = NULL; v->len = 0; v->cap = 0;
}

static char *sb_finish(char **buf, int *len, int *cap)
{
  (void)cap;  // suppress unused-parameter warning
  if (*len == 0) return NULL;
  (*buf)[*len] = '\0';
  char *out = strdup(*buf);
  if (!out) { perror("trade: strdup"); exit(1); }
  *len = 0;
  return out;
}

static void sb_add(char **buf, int *len, int *cap, char c)
{
  if (*buf == NULL) {
    *cap = 64;
    *buf = malloc((size_t)(*cap));
    if (!*buf) { perror("trade: malloc"); exit(1); }
    *len = 0;
  }
  if (*len + 2 >= *cap) {
    *cap *= 2;
    char *tmp = realloc(*buf, (size_t)(*cap));
    if (!tmp) { perror("trade: realloc"); exit(1); }
    *buf = tmp;
  }
  (*buf)[(*len)++] = c;
}

static strvec tokenize(const char *line, int *parse_err)
{
  *parse_err = 0;
  strvec out; sv_init(&out);

  char *buf = NULL;
  int blen = 0, bcap = 0;

  enum { ST_NORMAL, ST_SQ, ST_DQ } st = ST_NORMAL;

  for (size_t i = 0; line[i] != '\0'; i++) {
    char c = line[i];

    if (st == ST_NORMAL) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        char *t = sb_finish(&buf, &blen, &bcap);
        if (t) sv_push(&out, t);
        continue;
      }
      if (c == '\'') { st = ST_SQ; continue; }
      if (c == '"')  { st = ST_DQ; continue; }

      if (c == '|') {
        char *t = sb_finish(&buf, &blen, &bcap);
        if (t) sv_push(&out, t);
        sv_push(&out, strdup("|"));
        continue;
      }

      if (c == '\\') {
        // escape next char if exists
        char n = line[i + 1];
        if (n != '\0') { sb_add(&buf, &blen, &bcap, n); i++; continue; }
        // trailing backslash -> treat as literal
        sb_add(&buf, &blen, &bcap, c);
        continue;
      }

      sb_add(&buf, &blen, &bcap, c);
    }
    else if (st == ST_SQ) {
      if (c == '\'') { st = ST_NORMAL; continue; }
      sb_add(&buf, &blen, &bcap, c);
    }
    else { // ST_DQ
      if (c == '"') { st = ST_NORMAL; continue; }
      if (c == '\\') {
        char n = line[i + 1];
        if (n != '\0') { sb_add(&buf, &blen, &bcap, n); i++; continue; }
        sb_add(&buf, &blen, &bcap, c);
        continue;
      }
      sb_add(&buf, &blen, &bcap, c);
    }
  }

  if (st != ST_NORMAL) {
    *parse_err = 1; // unclosed quote
  }

  char *t = sb_finish(&buf, &blen, &bcap);
  if (t) sv_push(&out, t);

  free(buf);
  return out;
}

// ====== dispatch ======
typedef enum {
  CMD_PARENT_BUILTIN,   // help/exit/cd/pwd/start/stop...
  CMD_EXEC_ALLOWED,     // can exec (and pipe)
  CMD_UNKNOWN
} cmd_kind;

static cmd_kind classify_parent_builtin(const char *cmd)
{
  for (int i = 0; i < num_builtins(); i++) {
    if (strcmp(cmd, builtin_str[i]) == 0) return CMD_PARENT_BUILTIN;
  }
  return CMD_UNKNOWN;
}

// Build exec argv for allowed exec-style commands.
// `argv_out` must be freed by caller (free(argv_out) only, not strings).
static cmd_kind build_exec_argv(char **args, char ***argv_out)
{
  *argv_out = NULL;
  if (!args || !args[0]) return CMD_UNKNOWN;

  // exec-style allowed commands:
  // log/config/backup/restore -> python3 tool ...
  // nano/ls/cat/grep -> direct
  // scat -> sudo cat ...
  // update -> (sudo) bash UPDATE_TOOL ...

  if (strcmp(args[0], "log") == 0) {
  int count = 0;
  while (args[count] != NULL) count++;

  // log [ARGS...] -> sudo python3 LOG_TOOL [ARGS...]
  // argv: sudo, python3, LOG_TOOL, (count-1 args), NULL
  // or if sudo not available: python3, LOG_TOOL, ...
  int use_sudo = g_use_sudo ? 1 : 0;
  int base = use_sudo ? 3 : 2; // [sudo python3 LOG_TOOL] or [python3 LOG_TOOL]

  char **argv = calloc((size_t)(base + (count - 1) + 1), sizeof(char*));
  if (!argv) { perror("trade: calloc"); return CMD_UNKNOWN; }

  int i = 0;
  if (use_sudo) argv[i++] = (char*)SUDO;
  argv[i++] = (char*)PYTHON3;
  argv[i++] = (char*)LOG_TOOL;

  for (int j = 1; j < count; j++) argv[i++] = args[j];
  argv[i] = NULL;

  *argv_out = argv;
  return CMD_EXEC_ALLOWED;
}

  if (strcmp(args[0], "config") == 0) {
    build_passthrough_argv(args, PYTHON3, CONFIG_TOOL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }
  if (strcmp(args[0], "backup") == 0) {
    build_passthrough_argv(args, PYTHON3, BACKUP_TOOL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }
  if (strcmp(args[0], "restore") == 0) {
    build_passthrough_argv(args, PYTHON3, RESTORE_TOOL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }

  if (strcmp(args[0], "nano") == 0) {
    build_passthrough_argv(args, NANO, NULL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }
  if (strcmp(args[0], "ls") == 0) {
    build_passthrough_argv(args, LS, NULL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }
  if (strcmp(args[0], "cat") == 0) {
    build_passthrough_argv(args, CAT, NULL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }
  if (strcmp(args[0], "grep") == 0) {
    build_passthrough_argv(args, GREP, NULL, argv_out);
    return (*argv_out) ? CMD_EXEC_ALLOWED : CMD_UNKNOWN;
  }

  if (strcmp(args[0], "scat") == 0) {
    // scat [ARGS...] -> sudo cat [ARGS...]
    int count = 0;
    while (args[count] != NULL) count++;
    char **argv = calloc((size_t)count + 2, sizeof(char*));
    if (!argv) { perror("trade: calloc"); return CMD_UNKNOWN; }
    int i = 0;
    argv[i++] = (char*)SUDO;
    argv[i++] = (char*)CAT;
    for (int j = 1; j < count; j++) argv[i++] = args[j];
    argv[i] = NULL;
    *argv_out = argv;
    return CMD_EXEC_ALLOWED;
  }

  if (strcmp(args[0], "update") == 0) {
    // update [ARGS...] -> (sudo) bash UPDATE_TOOL [ARGS...]
    int count = 0;
    while (args[count] != NULL) count++;

    int use_sudo = g_use_sudo ? 1 : 0; // prefer sudo when available
    // argv: [sudo] bash UPDATE_TOOL + (count-1 args) + NULL
    char **argv = calloc((size_t)(count + 3), sizeof(char*));
    if (!argv) { perror("trade: calloc"); return CMD_UNKNOWN; }

    int i = 0;
    if (use_sudo) argv[i++] = (char*)SUDO;
    argv[i++] = (char*)BASH;
    argv[i++] = (char*)UPDATE_TOOL;
    for (int j = 1; j < count; j++) argv[i++] = args[j];
    argv[i] = NULL;

    *argv_out = argv;
    return CMD_EXEC_ALLOWED;
  }

  return CMD_UNKNOWN;
}

// Convert a slice of tokens into args[] (NULL-terminated) without copying strings.
// tokens are owned elsewhere; args array must be freed by caller.
static char **tokens_to_args(char **tokens, int start, int end_exclusive)
{
  int n = end_exclusive - start;
  if (n <= 0) return NULL;

  char **args = calloc((size_t)n + 1, sizeof(char*));
  if (!args) { perror("trade: calloc"); return NULL; }
  for (int i = 0; i < n; i++) args[i] = tokens[start + i];
  args[n] = NULL;
  return args;
}

// ====== pipeline executor ======
static int exec_pipeline(strvec *tokv)
{
  int (*pipes)[2] = NULL;
  int npipes = 0;
  pid_t *pids = NULL;
  char ***argvs = NULL;
  int *starts = NULL;
  int *ends = NULL;

  // split by '|'
  int ncmd = 1;
  for (int i = 0; i < tokv->len; i++) {
    if (strcmp(tokv->items[i], "|") == 0) ncmd++;
  }

  // build command ranges
  starts = calloc((size_t)ncmd, sizeof(int));
  ends   = calloc((size_t)ncmd, sizeof(int));
  if (!starts || !ends) { perror("trade: calloc"); free(starts); free(ends); return 1; }

  int ci = 0;
  int s = 0;
  for (int i = 0; i <= tokv->len; i++) {
    if (i == tokv->len || strcmp(tokv->items[i], "|") == 0) {
      starts[ci] = s;
      ends[ci] = i;
      ci++;
      s = i + 1;
    }
  }

  // validate and build exec argv for each stage
  argvs = calloc((size_t)ncmd, sizeof(char**));
  if (!argvs) { perror("trade: calloc"); free(starts); free(ends); return 1; }

  for (int k = 0; k < ncmd; k++) {
    char **args = tokens_to_args(tokv->items, starts[k], ends[k]);
    if (!args || !args[0]) {
      fprintf(stderr, "trade: invalid pipeline (empty command)\n");
      free(args);
      goto fail;
    }

    // parent-only builtin is not allowed in pipeline
    if (classify_parent_builtin(args[0]) == CMD_PARENT_BUILTIN) {
      fprintf(stderr, "trade: '%s' cannot be used in a pipeline\n", args[0]);
      free(args);
      goto fail;
    }

    char **exec_argv = NULL;
    if (build_exec_argv(args, &exec_argv) != CMD_EXEC_ALLOWED || !exec_argv) {
      fprintf(stderr, "trade: command not allowed in pipeline: %s\n", args[0]);
      free(args);
      goto fail;
    }

    argvs[k] = exec_argv;
    free(args);
  }

  // create pipes
  if (ncmd > 1) {
    npipes = ncmd - 1;
    pipes = calloc((size_t)npipes, sizeof(int[2]));
    if (!pipes) { perror("trade: calloc"); goto fail; }

    // init to -1 so fail-path close is safe even if pipe() fails mid-way
    for (int i = 0; i < npipes; i++) {
      pipes[i][0] = -1;
      pipes[i][1] = -1;
    }

    for (int i = 0; i < npipes; i++) {
      if (pipe(pipes[i]) != 0) {
        perror("trade: pipe");
        goto fail;
      }
    }
  }

  pids = calloc((size_t)ncmd, sizeof(pid_t));
  if (!pids) { perror("trade: calloc"); goto fail; }

  // fork each stage
  for (int i = 0; i < ncmd; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      perror("trade: fork");
      goto fail;
    }
    if (pid == 0) {
      // child: wire stdin/stdout
      if (pipes && npipes > 0) {
        if (i > 0) {
          dup2(pipes[i - 1][0], STDIN_FILENO);
        }
        if (i < ncmd - 1) {
          dup2(pipes[i][1], STDOUT_FILENO);
        }
        // close all pipe fds
        for (int j = 0; j < npipes; j++) {
          if (pipes[j][0] != -1) close(pipes[j][0]);
          if (pipes[j][1] != -1) close(pipes[j][1]);
        }
      }
      execvp(argvs[i][0], argvs[i]);
      fprintf(stderr, "trade: execvp failed: %s (%s)\n", argvs[i][0], strerror(errno));
      _exit(127);
    }
    pids[i] = pid;
  }

  // parent: close pipes
  if (pipes && npipes > 0) {
    for (int j = 0; j < npipes; j++) {
      if (pipes[j][0] != -1) close(pipes[j][0]);
      if (pipes[j][1] != -1) close(pipes[j][1]);
      pipes[j][0] = -1;
      pipes[j][1] = -1;
    }
  }

  // wait
  int last_rc = 0;
  for (int i = 0; i < ncmd; i++) {
    int status = 0;
    if (waitpid(pids[i], &status, 0) < 0) {
      perror("trade: waitpid");
      last_rc = 1;
      continue;
    }
    if (i == ncmd - 1) {
      if (WIFEXITED(status)) last_rc = WEXITSTATUS(status);
      else if (WIFSIGNALED(status)) last_rc = 128 + WTERMSIG(status);
      else last_rc = 1;
    }
  }

  // cleanup
  free(pids);
  if (pipes) free(pipes);
  for (int i = 0; i < ncmd; i++) free(argvs[i]);
  free(argvs);
  free(starts);
  free(ends);

  (void)last_rc;
  return 1;

fail:
  if (pipes && npipes > 0) {
    for (int j = 0; j < npipes; j++) {
      if (pipes[j][0] != -1) close(pipes[j][0]);
      if (pipes[j][1] != -1) close(pipes[j][1]);
    }
  }
  if (pipes) free(pipes);
  if (pids) free(pids);
  if (argvs) {
    for (int i = 0; i < ncmd; i++) free(argvs[i]);
    free(argvs);
  }
  free(starts);
  free(ends);
  return 1;
}

// ====== single command executor ======
static int execute_single(strvec *tokv)
{
  // build args view
  char **args = tokens_to_args(tokv->items, 0, tokv->len);
  if (!args || !args[0]) { free(args); return 1; }

  // parent builtins
  cmd_kind pk = classify_parent_builtin(args[0]);
  if (pk == CMD_PARENT_BUILTIN) {
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(args[0], builtin_str[i]) == 0) {
        int rc = (*builtin_func[i])(args);
        free(args);
        return rc;
      }
    }
  }

  // exec-style allowed
  char **exec_argv = NULL;
  if (build_exec_argv(args, &exec_argv) == CMD_EXEC_ALLOWED && exec_argv) {
    int rc = run_cmd_capture_rc(exec_argv);
    if (rc != 0) fprintf(stderr, "trade: command failed (rc=%d)\n", rc);
    free(exec_argv);
    free(args);
    return 1;
  }

  fprintf(stderr, "trade: unknown/blocked command: %s (type 'help')\n", args[0]);
  free(args);
  return 1;
}

static int execute_line(const char *line)
{
  int perr = 0;
  strvec tokv = tokenize(line, &perr);
  if (perr) {
    fprintf(stderr, "trade: parse error (unclosed quote)\n");
    sv_free_all(&tokv);
    return 1;
  }
  if (tokv.len == 0) {
    sv_free_all(&tokv);
    return 1;
  }

  // if contains '|', run pipeline
  int has_pipe = 0;
  for (int i = 0; i < tokv.len; i++) {
    if (strcmp(tokv.items[i], "|") == 0) { has_pipe = 1; break; }
  }

  int rc;
  if (has_pipe) rc = exec_pipeline(&tokv);
  else rc = execute_single(&tokv);

  sv_free_all(&tokv);
  return rc;
}

// ====== IO ======
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
    status = execute_line(line);
    free(line);
  }
}

int main(void)
{
  // Start in HOME directory if available
  const char *home = getenv("HOME");
  if (home && *home) {
    (void)chdir(home);
  }

  detect_sudo();
  printf("AutoTrade Shell (trade)  sudo=%s  type 'help'\n", g_use_sudo ? "on" : "off");
  loop();
  return 0;
}
