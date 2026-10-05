#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#include "optimize.h"
#include "semantic.h"
#include "structtab.h"
#include "symtab.h"
#include "target.h"
#include "translate.h"
#include "wio.h"
#include "xalloc.h"

static int input_fd;
static FILE *output_file;

//
// Enum for output format
//
typedef enum {
    FORMAT_TAC, // Default: binary TAC
    FORMAT_YAML,
    FORMAT_DOT
} OutputFormat;

//
// Structure to hold parsed arguments
//
typedef struct {
    int verbose;             // -v or --verbose
    int help;                // -h or --help
    int debug;               // -D or --debug
    OutputFormat format;     // Output format (--tac, --yaml, --dot)
    const char *target_name; // -t/--target
    char *input_file;        // Input filename
    char *output_file;       // Output filename (optional)
    int no_unreachable;      // --no-unreachable
    int no_copy_prop;        // --no-copy-prop
    int no_cse;              // --no-cse
    int opt_max_iter;        // --opt-max-iter N; -1 = the default
    int no_dead_store;       // --no-dead-store
    int no_loop_rotate;      // --no-loop-rotate
    int no_ivsr;             // --no-ivsr
    int opt_debug;           // --opt-debug
    int verify;              // --verify
} Args;

//
// Function to print usage information
//
static void print_usage(const char *prog_name)
{
    const char *p = strrchr(prog_name, '/');
    if (p) {
        prog_name = p + 1;
    }
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "    %s [options] input-filename [output-filename]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    --tac               Emit TAC in binary format (default)\n");
    fprintf(stderr, "    --yaml              Emit YAML format\n");
    fprintf(stderr, "    --dot               Emit Graphviz DOT script\n");
    fprintf(stderr, "    --no-unreachable    Disable unreachable code elimination\n");
    fprintf(stderr, "    --no-copy-prop      Disable copy propagation\n");
    fprintf(stderr, "    --no-cse            Disable common-subexpression elimination\n");
    fprintf(stderr, "    --no-dead-store     Disable dead store elimination\n");
    fprintf(stderr, "    --no-loop-rotate    Disable loop rotation (a loop tested at its bottom)\n");
    fprintf(stderr, "    --no-ivsr           Disable induction-variable strength reduction\n");
    fprintf(stderr, "    --opt-debug         Trace optimizer passes to stdout\n");
    fprintf(stderr, "    --opt-max-iter N    Run at most N optimizer rounds (0: to a fixed point)\n");
    fprintf(stderr, "    --verify            Check the TAC types (always on in debug builds)\n");
    fprintf(stderr, "    -t, --target NAME   Target architecture (default: riscv64)\n");
    fprintf(stderr, "    -v, --verbose       Enable verbose mode\n");
    fprintf(stderr, "    -D, --debug         Print debug information\n");
    fprintf(stderr, "    -h, --help          Show this help message\n");
    fprintf(stderr, "Known targets:\n");
    target_list();
}

//
// Initialize Args structure with default values
//
static void init_args(Args *args)
{
    args->verbose        = 0;
    args->help           = 0;
    args->debug          = 0;
    args->format         = FORMAT_TAC; // Default format
    args->target_name    = "riscv64";
    args->input_file     = NULL;
    args->output_file    = NULL;
    args->no_unreachable = 0;
    args->no_copy_prop   = 0;
    args->no_cse         = 0;
    args->opt_max_iter   = -1;
    args->no_dead_store  = 0;
    args->no_loop_rotate = 0;
    args->no_ivsr        = 0;
    args->verify         = 0;
    args->opt_debug      = 0;
}

//
// Generate output filename from input filename based on format
//
static char *generate_output_filename(const char *input_file, OutputFormat format)
{
    // Find the last '.' in input_file to replace extension
    const char *ext     = strrchr(input_file, '.');
    size_t base_len     = ext ? (size_t)(ext - input_file) : strlen(input_file);
    const char *new_ext = (format == FORMAT_DOT)    ? ".dot"
                          : (format == FORMAT_YAML) ? ".yaml"
                                                    : ".tac";
    size_t new_ext_len  = strlen(new_ext);

    // Allocate memory for new filename
    char *filename = malloc(base_len + new_ext_len + 1);
    if (!filename) {
        fprintf(stderr, "Error: Memory allocation failed for output filename\n");
        return NULL;
    }

    // Copy base name and append new extension
    strncpy(filename, input_file, base_len);
    strcpy(filename + base_len, new_ext);
    return filename;
}

//
// Parse command-line arguments using getopt_long
//
static int parse_args(int argc, char *argv[], Args *args)
{
    static struct option long_options[] = {
        { "verbose", no_argument, 0, 'v' },        //
        { "help", no_argument, 0, 'h' },           //
        { "debug", no_argument, 0, 'D' },          //
        { "tac", no_argument, 0, 'T' },            //
        { "yaml", no_argument, 0, 'y' },           //
        { "dot", no_argument, 0, 'd' },            //
        { "target", required_argument, 0, 't' },   //
        { "no-unreachable", no_argument, 0, 256 }, //
        { "no-copy-prop", no_argument, 0, 257 },   //
        { "no-dead-store", no_argument, 0, 258 },  //
        { "opt-debug", no_argument, 0, 259 },      //
        { "verify", no_argument, 0, 260 },         //
        { "no-cse", no_argument, 0, 261 },         //
        { "opt-max-iter", required_argument, 0, 262 }, //
        { "no-loop-rotate", no_argument, 0, 263 }, //
        { "no-ivsr", no_argument, 0, 264 },        //
        {},                                        //
    };

    int opt;
    int option_index = 0;

    if (argc < 2) {
        // Show usage.
        args->help = 1;
        return 0;
    }
    while ((opt = getopt_long(argc, argv, "vhDt:", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'v':
            args->verbose = 1;
            break;
        case 'h':
            args->help = 1;
            return 0;
        case 'D':
            args->debug = 1;
            break;
        case 't':
            args->target_name = optarg;
            break;
        // Long options without short equivalents
        case 'y':
            args->format = FORMAT_YAML;
            break;
        case 'd':
            args->format = FORMAT_DOT;
            break;
        case 'T':
            args->format = FORMAT_TAC;
            break;
        case 256:
            args->no_unreachable = 1;
            break;
        case 257:
            args->no_copy_prop = 1;
            break;
        case 258:
            args->no_dead_store = 1;
            break;
        case 259:
            args->opt_debug = 1;
            break;
        case 260:
            args->verify = 1;
            break;
        case 261:
            args->no_cse = 1;
            break;
        case 262:
            args->opt_max_iter = atoi(optarg);
            break;
        case 263:
            args->no_loop_rotate = 1;
            break;
        case 264:
            args->no_ivsr = 1;
            break;
        case '?': // Unknown option
            return -1;
        }
    }

    // Check for input filename (required)
    if (optind < argc) {
        args->input_file = argv[optind++];
    } else {
        fprintf(stderr, "Error: Input filename is required\n");
        return -1;
    }

    // Check for output filename (optional)
    if (optind < argc) {
        args->output_file = argv[optind];
    } else {
        // Generate output filename based on input and format
        args->output_file = generate_output_filename(args->input_file, args->format);
        if (!args->output_file) {
            return -1;
        }
    }

    return 0;
}

static void open_input_output(const Args *args)
{
    if (args->verbose) {
        switch (args->format) {
        case FORMAT_TAC:
            printf("Emitting TAC in binary format to %s\n", args->output_file);
            break;
        case FORMAT_YAML:
            printf("Emitting YAML format to %s\n", args->output_file);
            break;
        case FORMAT_DOT:
            printf("Emitting Graphviz DOT script to %s\n", args->output_file);
            break;
        }
    }
    input_fd = open(args->input_file, O_RDONLY);
    if (input_fd < 0) {
        perror(args->input_file);
        exit(1);
    }
    output_file = stdout;
    if (args->output_file[0] != '-') {
        output_file = fopen(args->output_file, "w");
    }
}

static void close_output(const Args *args)
{
    (void)args;
    close(input_fd);
    if (output_file != stdout) {
        fclose(output_file);
    }
}

static void emit_tac_toplevel(const Args *args, WFILE *tac_out, const Tac_TopLevel *tac)
{
    switch (args->format) {
    default:
    case FORMAT_TAC:
        tac_export_toplevel(tac_out, tac);
        break;
    case FORMAT_YAML:
        tac_export_yaml(output_file, tac);
        fflush(output_file);
        break;
    case FORMAT_DOT:
        tac_export_dot(output_file, tac);
        fflush(output_file);
        break;
    }
}

//
// Main processing function
//
void process_file(const Args *args)
{
    target_config = target_lookup(args->target_name);
    if (!target_config) {
        fprintf(stderr, "Unknown target '%s'. Known targets:\n", args->target_name);
        target_list();
        exit(1);
    }

    OptFlags flags         = opt_flags_default();
    flags.unreachable_elim = !args->no_unreachable;
    flags.copy_propagation = !args->no_copy_prop;
    if (args->no_cse)
        flags.cse = false;
    if (args->opt_max_iter >= 0)
        flags.max_iterations = args->opt_max_iter;
    else if (getenv("VCC_OPT_MAX_ITER")) // for bisecting a whole build
        flags.max_iterations = atoi(getenv("VCC_OPT_MAX_ITER"));
    flags.dead_store_elim  = !args->no_dead_store;
    flags.loop_rotate      = !args->no_loop_rotate;
    flags.ivsr             = !args->no_ivsr;
    if (args->verify)
        translate_verify = 1;
    flags.debug            = args->opt_debug;

    if (args->verbose) {
        printf("Processing %s in verbose mode\n", args->input_file);
    }
    if (args->debug) {
        printf("Debug: Format = %d, Input = %s, Output = %s\n", args->format, args->input_file,
               args->output_file);
        translator_debug = 1;
        // import_debug     = 1;
        // export_debug     = 1;
        // wio_debug        = 1;
        // xalloc_debug     = 1;
    }
    open_input_output(args);

    WFILE input;
    ast_import_open(&input, input_fd);

    WFILE tac_out;
    int tac_out_ready = 0;
    if (args->format == FORMAT_TAC) {
        if (wdopen(&tac_out, output_file == stdout ? STDOUT_FILENO : fileno(output_file), "w") <
            0) {
            fprintf(stderr, "Cannot reopen output file\n");
            exit(1);
        }
        tac_out_ready = 1;
        tac_export_begin_stream(&tac_out);
    }

    symtab_init();
    structtab_init();
    // Unit-wide temp/label counter, shared across every function so `%N` names stay
    // unique within the translation unit (required by the single-file backends —
    // see translate.h).  Reset to 0 once, here, at the start of the unit.
    int label_seq = 0;
    translate_unit_begin();
    for (;;) {
        ExternalDecl *ast = import_external_decl(&input);
        if (!ast)
            break;

        if (args->debug) {
            print_external_decl(stdout, ast, 0);
        }

        // Typecheck definitions and uses of functions and variables.
        // Annotate loops and break/continue statements — loop labels share the
        // unit-wide counter with the translator's temporaries.
        typecheck_decl(ast, &label_seq);

        // Convert the AST to TAC and optimize. Each function carries its own
        // params + locals, so the optimizer needs no whole-program context.
        Tac_TopLevel *tac = translate(ast, flags, &label_seq);
        free_external_decl(ast);
        if (tac) {
            for (const Tac_TopLevel *t = tac; t; t = t->next) {
                if (args->debug) {
                    tac_print_toplevel(stdout, t, 0);
                }
                emit_tac_toplevel(args, tac_out_ready ? &tac_out : NULL, t);
            }
            tac_free_toplevel(tac);
        }
    }
    wclose(&input);

    // Names this unit references but does not define, with their types.
    Tac_TopLevel *externs = translate_unit_end();
    for (const Tac_TopLevel *t = externs; t; t = t->next) {
        if (args->debug)
            tac_print_toplevel(stdout, t, 0);
        emit_tac_toplevel(args, tac_out_ready ? &tac_out : NULL, t);
    }
    tac_free_toplevel(externs);

    if (tac_out_ready) {
        tac_export_end_stream(&tac_out);
        wclose(&tac_out);
    }
    close_output(args);

    symtab_destroy();
    structtab_destroy();
    if (args->debug) {
        xreport_lost_memory();
    }
    xfree_all();
}

//
// Error handling
//
void _Noreturn fatal_error(const char *message, ...)
{
    fprintf(stderr, "Fatal error: ");

    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    Args args;
    init_args(&args);

    if (parse_args(argc, argv, &args) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (args.help) {
        print_usage(argv[0]);
        return 0;
    }

    // Pass args to backend for processing
    process_file(&args);

    return 0;
}
