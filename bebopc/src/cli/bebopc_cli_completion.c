#include "bebopc_cli_completion.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)
#endif

static void _generate_bash(FILE* out)
{
  fprintf(out,
        "_bebopc() {\n"
        "    local cur prev cmd\n"
        "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
        "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
        "    cmd=\"${COMP_WORDS[1]}\"\n"
        "\n"
        "    # Commands\n"
        "    local commands=\"");

#define X(N, n, a, o, d, h) fprintf(out, "%s ", n);
  BEBOPC_COMMANDS(X)
#undef X

  fprintf(out, "\"\n\n" "    # Options with values\n" "    case \"$prev\" in\n");

#define X(N, s, l, v, vn, f, d) \
  if ((v) && (const void*)(vn) != NULL) { \
    if ((s) != 0) \
      fprintf(out, "        -%c|--%s)\n            return 0\n            ;;\n", (s), l); \
    else \
      fprintf(out, "        --%s)\n            return 0\n            ;;\n", l); \
  }
  BEBOPC_OPTIONS(X)
#undef X

  fprintf(out,
        "    esac\n"
        "\n"
        "    # Complete commands at position 1\n"
        "    if [[ $COMP_CWORD -eq 1 ]]; then\n"
        "        COMPREPLY=($(compgen -W \"$commands\" -- \"$cur\"))\n"
        "        return 0\n"
        "    fi\n"
        "\n"
        "    # Complete options\n"
        "    case \"$cmd\" in\n"
        "        build|check|watch)\n"
        "            local opts=\"");

#define X(N, s, l, v, vn, f, d) \
  if ((s) != 0) \
    fprintf(out, "-%c ", (s)); \
  fprintf(out, "--%s ", l);
  BEBOPC_OPTIONS(X)
#undef X

  fprintf(out, "\"\n"
                 "            COMPREPLY=($(compgen -W \"$opts\" -- \"$cur\"))\n"
                 "            ;;\n"
                 "        completion)\n"
                 "            COMPREPLY=($(compgen -W \"");

#define X(N, n, d) fprintf(out, "%s ", n);
  BEBOPC_SHELLS(X)
#undef X

  fprintf(out, "\" -- \"$cur\"))\n"
                 "            ;;\n"
                 "        help)\n"
                 "            COMPREPLY=($(compgen -W \"$commands\" -- \"$cur\"))\n"
                 "            ;;\n"
                 "    esac\n"
                 "}\n"
                 "\n"
                 "complete -F _bebopc -o default bebopc\n");
}

static void _generate_zsh(FILE* out)
{
  fprintf(out,
        "#compdef bebopc\n"
        "\n"
        "_bebopc() {\n"
        "    local -a commands\n"
        "    commands=(\n");

#define X(N, n, a, o, d, h) fprintf(out, "        '%s:%s'\n", n, d);
  BEBOPC_COMMANDS(X)
#undef X

  fprintf(out, "    )\n" "\n" "    local -a options\n" "    options=(\n");

#define X(N, s, l, v, vn, f, d) \
  if ((s) != 0) { \
    if (v) \
      fprintf( \
          out, "        '-%c[%s]::%s:' \\\n", (s), d, (const void*)(vn) != NULL ? (vn) : "value" \
      ); \
    else \
      fprintf(out, "        '-%c[%s]' \\\n", (s), d); \
  } \
  if (v) \
    fprintf( \
        out, "        '--%s[%s]::%s:' \\\n", l, d, (const void*)(vn) != NULL ? (vn) : "value" \
    ); \
  else \
    fprintf(out, "        '--%s[%s]' \\\n", l, d);
  BEBOPC_OPTIONS(X)
#undef X

  fprintf(out, "    )\n" "\n" "    local -a shells\n" "    shells=(");

#define X(N, n, d) fprintf(out, "'%s:%s' ", n, d);
  BEBOPC_SHELLS(X)
#undef X

  fprintf(out, ")\n"
                 "\n"
                 "    _arguments -C \\\n"
                 "        '1:command:->cmd' \\\n"
                 "        '*::arg:->args'\n"
                 "\n"
                 "    case \"$state\" in\n"
                 "        cmd)\n"
                 "            _describe 'command' commands\n"
                 "            ;;\n"
                 "        args)\n"
                 "            case \"${words[1]}\" in\n"
                 "                build|check|watch)\n"
                 "                    _arguments $options '*:file:_files -g \"*.bop\"'\n"
                 "                    ;;\n"
                 "                completion)\n"
                 "                    _describe 'shell' shells\n"
                 "                    ;;\n"
                 "                help)\n"
                 "                    _describe 'command' commands\n"
                 "                    ;;\n"
                 "            esac\n"
                 "            ;;\n"
                 "    esac\n"
                 "}\n"
                 "\n"
                 "_bebopc\n");
}

static void _generate_fish(FILE* out)
{
  fprintf(out, "# bebopc fish completion\n\n");

  fprintf(out, "complete -c bebopc -f\n\n");

  fprintf(out, "# Commands\n");
#define X(N, n, a, o, d, h) \
  fprintf(out, "complete -c bebopc -n __fish_use_subcommand -a %s -d '%s'\n", n, d);
  BEBOPC_COMMANDS(X)
#undef X

  fprintf(out, "\n# Global options\n");
#define X(N, s, l, v, vn, f, d) \
  if ((s) != 0) \
    fprintf(out, "complete -c bebopc -s %c -l %s -d '%s'", (s), l, d); \
  else \
    fprintf(out, "complete -c bebopc -l %s -d '%s'", l, d); \
  if (v) \
    fprintf(out, " -r"); \
  fprintf(out, "\n");
  BEBOPC_OPTIONS(X)
#undef X

  fprintf(out, "\n# Completion command arguments\n");
#define X(N, n, d) \
  fprintf( \
      out, \
      "complete -c bebopc -n '__fish_seen_subcommand_from completion' -a " "%s -d '%s'\n", \
      n, \
      d \
  );
  BEBOPC_SHELLS(X)
#undef X

  fprintf(out, "\n# Help command arguments\n");
#define X(N, n, a, o, d, h) \
  fprintf(out, "complete -c bebopc -n '__fish_seen_subcommand_from help' -a %s -d " "'%s'\n", n, d);
  BEBOPC_COMMANDS(X)
#undef X

  fprintf(out, "\n# File completion for build/check\n");
  fprintf(
      out,
      "complete -c bebopc -n '__fish_seen_subcommand_from build check' -F " "-a '*.bop'\n"
  );
}

static void _generate_powershell(FILE* out)
{
  fprintf(out, "# bebopc PowerShell completion\n" "\n" "$script:commands = @(\n");

#define X(N, n, a, o, d, h) fprintf(out, "    @{ Name = '%s'; Description = '%s' }\n", n, d);
  BEBOPC_COMMANDS(X)
#undef X

  fprintf(out, ")\n\n$script:options = @(\n");

#define X(N, s, l, v, vn, f, d) \
  fprintf( \
      out, \
      "    @{ Short = '%c'; Long = '%s'; HasValue = $%s; Description = " "'%s' }\n", \
      s ? s : ' ', \
      l, \
      v ? "true" : "false", \
      d \
  );
  BEBOPC_OPTIONS(X)
#undef X

  fprintf(out, ")\n\n$script:shells = @(");

#define X(N, n, d) fprintf(out, "'%s', ", n);
  BEBOPC_SHELLS(X)
#undef X

  fprintf(out, "'')\n"
                 "\n"
                 "Register-ArgumentCompleter -Native -CommandName bebopc -ScriptBlock {\n"
                 "    param($wordToComplete, $commandAst, $cursorPosition)\n"
                 "\n"
                 "    $tokens = $commandAst.CommandElements\n"
                 "    $cmd = if ($tokens.Count -gt 1) { $tokens[1].Extent.Text } else { '' }\n"
                 "\n"
                 "    # Complete commands\n"
                 "    if ($tokens.Count -le 2 -and $wordToComplete -notmatch '^-') {\n"
                 "        $script:commands | Where-Object { $_.Name -like \"$wordToComplete*\" } | ForEach-Object {\n"
                 "            [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', $_.Description)\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "\n"
                 "    # Complete shell names for completion command\n"
                 "    if ($cmd -eq 'completion') {\n"
                 "        $script:shells | Where-Object { $_ -like \"$wordToComplete*\" -and $_ -ne '' } | ForEach-Object {\n"
                 "            [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', \"$_ completion\")\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "\n"
                 "    # Complete command names for help command\n"
                 "    if ($cmd -eq 'help') {\n"
                 "        $script:commands | Where-Object { $_.Name -like \"$wordToComplete*\" } | ForEach-Object {\n"
                 "            [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'ParameterValue', $_.Description)\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "\n"
                 "    # Complete options\n"
                 "    if ($wordToComplete -match '^-') {\n"
                 "        $script:options | ForEach-Object {\n"
                 "            if ($_.Short -ne ' ' -and \"-$($_.Short)\" -like \"$wordToComplete*\") {\n"
                 "                [System.Management.Automation.CompletionResult]::new(\"-$($_.Short)\", \"-$($_.Short)\", 'ParameterName', $_.Description)\n"
                 "            }\n"
                 "            if (\"--$($_.Long)\" -like \"$wordToComplete*\") {\n"
                 "                [System.Management.Automation.CompletionResult]::new(\"--$($_.Long)\", \"--$($_.Long)\", 'ParameterName', $_.Description)\n"
                 "            }\n"
                 "        }\n"
                 "    }\n"
                 "}\n");
}

void cli_generate_completion(cli_shell_t shell, FILE* out)
{
  switch (shell) {
    case CLI_SHELL_BASH:
      _generate_bash(out);
      break;
    case CLI_SHELL_ZSH:
      _generate_zsh(out);
      break;
    case CLI_SHELL_FISH:
      _generate_fish(out);
      break;
    case CLI_SHELL_POWERSHELL:
      _generate_powershell(out);
      break;
    default:
      break;
  }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
