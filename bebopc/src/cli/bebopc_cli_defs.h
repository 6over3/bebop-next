#ifndef BEBOPC_CLI_DEFS_H
#define BEBOPC_CLI_DEFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Option flags for scope
#define CLI_OPT_FLAG_ROOT   (1 << 0)  // Only valid before command
#define CLI_OPT_FLAG_BUILD  (1 << 1)
#define CLI_OPT_FLAG_CHECK  (1 << 2)
#define CLI_OPT_FLAG_WATCH  (1 << 3)
#define CLI_OPT_FLAG_FMT    (1 << 4)
#define CLI_OPT_FLAG_ALL    (CLI_OPT_FLAG_BUILD | CLI_OPT_FLAG_CHECK | CLI_OPT_FLAG_WATCH | CLI_OPT_FLAG_FMT)

// X(NAME, short, long, has_value, value_name, flags, description)
// CLI_OPT_FLAG_ROOT = shown in root help, valid before command
// CLI_OPT_FLAG_BUILD/CHECK/WATCH = valid for that command
// CLI_OPT_FLAG_ALL = valid for all commands (build/check/watch)
#define BEBOPC_OPTIONS(X) \
  X(HELP, 'h', "help", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Show help") \
  X(VERSION, 'V', "version", false, NULL, CLI_OPT_FLAG_ROOT, "Show version") \
  X(LLM, 0, "llm", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Print LLM usage instructions") \
  X(CONFIG, 'c', "config", true, "FILE", CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Config file path") \
  X(COLOR, 0, "color", true, "MODE", CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Color mode: auto|always|never") \
  X(FORMAT, 0, "format", true, "FMT", CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Output format: terminal|json|msbuild|xcode") \
  X(VERBOSE, 'v', "verbose", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Verbose output") \
  X(QUIET, 'q', "quiet", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Suppress non-error output") \
  X(NO_WARN, 0, "no-warn", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Suppress warnings") \
  X(TRACE, 0, "trace", false, NULL, CLI_OPT_FLAG_ROOT | CLI_OPT_FLAG_ALL, "Enable trace output") \
  X(PLUGIN, 0, "plugin", true, "NAME=PATH", CLI_OPT_FLAG_BUILD, "Specify plugin executable path") \
  X(OPTION, 'D', "option", true, "KEY=VAL", CLI_OPT_FLAG_BUILD, "Set plugin option") \
  X(EXCLUDE, 'x', "exclude", true, "GLOB", CLI_OPT_FLAG_ALL, "Add exclude pattern") \
  X(INCLUDE, 'I', "include", true, "DIR", CLI_OPT_FLAG_ALL, "Add import search path") \
  X(EMIT_SOURCE_INFO, 0, "emit-source-info", false, NULL, CLI_OPT_FLAG_BUILD, "Include source locations in descriptors") \
  X(NO_EMIT, 0, "no-emit", false, NULL, CLI_OPT_FLAG_WATCH, "Check only, no code generation") \
  X(PRESERVE_OUTPUT, 0, "preserve-output", false, NULL, CLI_OPT_FLAG_WATCH, "Don't clear screen on rebuild") \
  X(FMT_CHECK, 0, "check", false, NULL, CLI_OPT_FLAG_FMT, "Check if files are formatted (exit 1 if not)") \
  X(FMT_DIFF, 0, "diff", false, NULL, CLI_OPT_FLAG_FMT, "Show diff of changes") \
  X(FMT_WRITE, 'w', "write", false, NULL, CLI_OPT_FLAG_FMT, "Write formatted output to files (default)")

#define BEBOPC_COMMANDS(X) \
  X(BUILD, \
    "build", \
    "<files...> --<gen>_out=<dir>", \
    true, \
    "Compile schemas and generate code", \
    "\nExamples:\n" \
    "  bebopc build schema.bop --c_out=./generated\n" \
    "  bebopc build *.bop --c_out=./out --ts_out=./out\n" \
    "  bebopc build -I ./shared -I ./vendor schema.bop --c_out=./out\n" \
    "  bebopc build --plugin=custom=/path/to/plugin foo.bop --custom_out=./out\n" \
    "\nPlugins are discovered as bebopc-gen-<name> in PATH or next to bebopc.\n" \
    "Use --plugin to specify an explicit path.\n" \
    "Use -I/--include to add directories for resolving imports.") \
  X(WATCH, "watch", "", true, "Watch for changes and recompile", \
    "Uses sources/exclude patterns from bebop.yml config.") \
  X(INIT, "init", "", false, "Create bebop.yml config template", \
    "Creates a bebop.yml template in the current directory.") \
  X(CHECK, "check", "<files...>", true, "Validate schemas without codegen", \
    "Parses and validates without generating code.") \
  X(FMT, "fmt", "<files...>", true, "Format schema files", \
    "\nExamples:\n" \
    "  bebopc fmt schema.bop          # Format and write in-place\n" \
    "  bebopc fmt *.bop               # Format all .bop files\n" \
    "  bebopc fmt --check *.bop       # Check formatting (exit 1 if unformatted)\n" \
    "  bebopc fmt --diff schema.bop   # Show diff without writing\n" \
    "\nBy default, formats files in-place. Use --check in CI to verify formatting.") \
  X(LSP, "lsp", "", false, "Start LSP server for editor integration", \
    "Communicates via stdin/stdout using the Language Server Protocol.") \
  X(COMPLETION, "completion", "<shell>", false, "Generate shell completion script", NULL) \
  X(HELP, "help", "[command]", false, "Show help for a command", NULL) \
  X(VERSION, "version", "", false, "Show version information", NULL)

#define BEBOPC_SHELLS(X) \
  X(BASH, "bash", "Bash completion script") \
  X(ZSH, "zsh", "Zsh completion script") \
  X(FISH, "fish", "Fish completion script") \
  X(POWERSHELL, "powershell", "PowerShell completion script")

typedef enum {
#define X(N, s, l, v, vn, f, d) CLI_OPT_##N,
  BEBOPC_OPTIONS(X)
#undef X
  CLI_OPT_COUNT
} cli_opt_t;

typedef enum {
#define X(N, n, a, o, d, h) CLI_CMD_##N,
  BEBOPC_COMMANDS(X)
#undef X
  CLI_CMD_COUNT,
  CLI_CMD_NONE = -1
} cli_cmd_t;

typedef enum {
#define X(N, n, d) CLI_SHELL_##N,
  BEBOPC_SHELLS(X)
#undef X
  CLI_SHELL_COUNT,
  CLI_SHELL_UNKNOWN = -1
} cli_shell_t;

typedef struct {
  char short_name;
  const char* long_name;
  bool has_value;
  const char* value_name;
  uint32_t flags;
  const char* description;
} cli_opt_def_t;

typedef struct {
  const char* name;
  const char* args_pattern;
  bool show_options;
  const char* description;
  const char* extended_help;
} cli_cmd_def_t;

typedef struct {
  const char* name;
  const char* description;
} cli_shell_def_t;

static const cli_opt_def_t cli_options[] = {
#define X(N, s, l, v, vn, f, d) {s, l, v, vn, f, d},
    BEBOPC_OPTIONS(X)
#undef X
};

static const cli_cmd_def_t cli_commands[] = {
#define X(N, n, a, o, d, h) {n, a, o, d, h},
    BEBOPC_COMMANDS(X)
#undef X
};

static const cli_shell_def_t cli_shells[] = {
#define X(N, n, d) {n, d},
    BEBOPC_SHELLS(X)
#undef X
};

#endif
