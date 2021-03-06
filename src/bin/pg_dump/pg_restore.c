/*-------------------------------------------------------------------------
 *
 * pg_restore.c
 *	pg_restore is an utility extracting postgres database definitions
 *	from a backup archive created by pg_dump using the archiver
 *	interface.
 *
 *	pg_restore will read the backup archive and
 *	dump out a script that reproduces
 *	the schema of the database in terms of
 *		  user-defined types
 *		  user-defined functions
 *		  tables
 *		  indexes
 *		  aggregates
 *		  operators
 *		  ACL - grant/revoke
 *
 * the output script is SQL that is understood by PostgreSQL
 *
 * Basic process in a restore operation is:
 *
 *	Open the Archive and read the TOC.
 *	Set flags in TOC entries, and *maybe* reorder them.
 *	Generate script to stdout
 *	Exit
 *
 * Copyright (c) 2000, Philip Warner
 *		Rights are granted to use this software in any way so long
 *		as this notice is not removed.
 *
 *	The author is not responsible for loss or damages that may
 *	result from its use.
 *
 *
 * IDENTIFICATION
 *		src/bin/pg_dump/pg_restore.c
 *
 *-------------------------------------------------------------------------
 */

#include "pg_backup_archiver.h"
#include "dumputils.h"

#include <ctype.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include <unistd.h>

#include "getopt_long.h"

extern char *optarg;
extern int	optind;

#ifdef ENABLE_NLS
#include <locale.h>
#endif


static void usage(const char *progname);

typedef struct option optType;

int
main(int argc, char **argv)
{
	RestoreOptions *opts;
	int			c;
	int			exit_code;
	Archive    *AH;
	char	   *inputFileSpec;
	static int	disable_triggers = 0;
	static int	no_data_for_failed_tables = 0;
	static int	outputNoTablespaces = 0;
	static int	use_setsessauth = 0;
	static int	skip_seclabel = 0;

	struct option cmdopts[] = {
		{"clean", 0, NULL, 'c'},
		{"create", 0, NULL, 'C'},
		{"data-only", 0, NULL, 'a'},
		{"dbname", 1, NULL, 'd'},
		{"exit-on-error", 0, NULL, 'e'},
		{"file", 1, NULL, 'f'},
		{"format", 1, NULL, 'F'},
		{"function", 1, NULL, 'P'},
		{"host", 1, NULL, 'h'},
		{"ignore-version", 0, NULL, 'i'},
		{"index", 1, NULL, 'I'},
		{"jobs", 1, NULL, 'j'},
		{"list", 0, NULL, 'l'},
		{"no-privileges", 0, NULL, 'x'},
		{"no-acl", 0, NULL, 'x'},
		{"no-owner", 0, NULL, 'O'},
		{"no-reconnect", 0, NULL, 'R'},
		{"port", 1, NULL, 'p'},
		{"no-password", 0, NULL, 'w'},
		{"password", 0, NULL, 'W'},
		{"schema", 1, NULL, 'n'},
		{"schema-only", 0, NULL, 's'},
		{"superuser", 1, NULL, 'S'},
		{"table", 1, NULL, 't'},
		{"trigger", 1, NULL, 'T'},
		{"use-list", 1, NULL, 'L'},
		{"username", 1, NULL, 'U'},
		{"verbose", 0, NULL, 'v'},
		{"single-transaction", 0, NULL, '1'},

		/*
		 * the following options don't have an equivalent short option letter
		 */
		{"disable-triggers", no_argument, &disable_triggers, 1},
		{"no-data-for-failed-tables", no_argument, &no_data_for_failed_tables, 1},
		{"no-tablespaces", no_argument, &outputNoTablespaces, 1},
		{"role", required_argument, NULL, 2},
		{"use-set-session-authorization", no_argument, &use_setsessauth, 1},
		{"no-security-label", no_argument, &skip_seclabel, 1},

		{NULL, 0, NULL, 0}
	};

	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pg_dump"));

	init_parallel_dump_utils();

	opts = NewRestoreOptions();

	progname = get_progname(argv[0]);

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage(progname);
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pg_restore (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}

	while ((c = getopt_long(argc, argv, "acCd:ef:F:h:iI:j:lL:n:Op:P:RsS:t:T:U:vwWxX:1",
							cmdopts, NULL)) != -1)
	{
		switch (c)
		{
			case 'a':			/* Dump data only */
				opts->dataOnly = 1;
				break;
			case 'c':			/* clean (i.e., drop) schema prior to create */
				opts->dropSchema = 1;
				break;
			case 'C':
				opts->createDB = 1;
				break;
			case 'd':
				opts->dbname = strdup(optarg);
				break;
			case 'e':
				opts->exit_on_error = true;
				break;
			case 'f':			/* output file name */
				opts->filename = strdup(optarg);
				break;
			case 'F':
				if (strlen(optarg) != 0)
					opts->formatName = strdup(optarg);
				break;
			case 'h':
				if (strlen(optarg) != 0)
					opts->pghost = strdup(optarg);
				break;
			case 'i':
				/* ignored, deprecated option */
				break;

			case 'j':			/* number of restore jobs */
				opts->number_of_jobs = atoi(optarg);
				break;

			case 'l':			/* Dump the TOC summary */
				opts->tocSummary = 1;
				break;

			case 'L':			/* input TOC summary file name */
				opts->tocFile = strdup(optarg);
				break;

			case 'n':			/* Dump data for this schema only */
				opts->schemaNames = strdup(optarg);
				break;

			case 'O':
				opts->noOwner = 1;
				break;

			case 'p':
				if (strlen(optarg) != 0)
					opts->pgport = strdup(optarg);
				break;
			case 'R':
				/* no-op, still accepted for backwards compatibility */
				break;
			case 'P':			/* Function */
				opts->selTypes = 1;
				opts->selFunction = 1;
				opts->functionNames = strdup(optarg);
				break;
			case 'I':			/* Index */
				opts->selTypes = 1;
				opts->selIndex = 1;
				opts->indexNames = strdup(optarg);
				break;
			case 'T':			/* Trigger */
				opts->selTypes = 1;
				opts->selTrigger = 1;
				opts->triggerNames = strdup(optarg);
				break;
			case 's':			/* dump schema only */
				opts->schemaOnly = 1;
				break;
			case 'S':			/* Superuser username */
				if (strlen(optarg) != 0)
					opts->superuser = strdup(optarg);
				break;
			case 't':			/* Dump data for this table only */
				opts->selTypes = 1;
				opts->selTable = 1;
				opts->tableNames = strdup(optarg);
				break;

			case 'U':
				opts->username = optarg;
				break;

			case 'v':			/* verbose */
				opts->verbose = 1;
				break;

			case 'w':
				opts->promptPassword = TRI_NO;
				break;

			case 'W':
				opts->promptPassword = TRI_YES;
				break;

			case 'x':			/* skip ACL dump */
				opts->aclsSkip = 1;
				break;

			case 'X':
				/* -X is a deprecated alternative to long options */
				if (strcmp(optarg, "disable-triggers") == 0)
					disable_triggers = 1;
				else if (strcmp(optarg, "no-data-for-failed-tables") == 0)
					no_data_for_failed_tables = 1;
				else if (strcmp(optarg, "no-tablespaces") == 0)
					outputNoTablespaces = 1;
				else if (strcmp(optarg, "use-set-session-authorization") == 0)
					use_setsessauth = 1;
				else if (strcmp(optarg, "no-security-label") == 0)
					skip_seclabel = 1;
				else
				{
					fprintf(stderr,
							_("%s: invalid -X option -- %s\n"),
							progname, optarg);
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				break;

			case '1':			/* Restore data in a single transaction */
				opts->single_txn = true;
				opts->exit_on_error = true;
				break;

			case 0:

				/*
				 * This covers the long options without a short equivalent,
				 * including those equivalent to -X xxx.
				 */
				break;

			case 2:				/* SET ROLE */
				opts->use_role = optarg;
				break;

			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	/* Get file name from command line */
	if (optind < argc)
		inputFileSpec = argv[optind++];
	else
		inputFileSpec = NULL;

	/* Complain if any arguments remain */
	if (optind < argc)
	{
		fprintf(stderr, _("%s: too many command-line arguments (first is \"%s\")\n"),
				progname, argv[optind]);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				progname);
		exit(1);
	}

	/* Should get at most one of -d and -f, else user is confused */
	if (opts->dbname)
	{
		if (opts->filename)
		{
			fprintf(stderr, _("%s: options -d/--dbname and -f/--file cannot be used together\n"),
					progname);
			fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
					progname);
			exit(1);
		}
		opts->useDB = 1;
	}

	/* Can't do single-txn mode with multiple connections */
	if (opts->single_txn && opts->number_of_jobs > 1)
	{
		fprintf(stderr, _("%s: cannot specify both --single-transaction and multiple jobs\n"),
				progname);
		exit(1);
	}

	opts->disable_triggers = disable_triggers;
	opts->noDataForFailedTables = no_data_for_failed_tables;
	opts->noTablespace = outputNoTablespaces;
	opts->use_setsessauth = use_setsessauth;
	opts->skip_seclabel = skip_seclabel;

	if (opts->formatName)
	{
		switch (opts->formatName[0])
		{
			case 'c':
			case 'C':
				opts->format = archCustom;
				break;

			case 'd':
			case 'D':
				opts->format = archDirectory;
				break;

			case 'f':
			case 'F':
				opts->format = archFiles;
				break;

			case 't':
			case 'T':
				opts->format = archTar;
				break;

			default:
				write_msg(NULL, "unrecognized archive format \"%s\"; please specify \"c\", \"d\" or \"t\"\n",
						  opts->formatName);
				exit(1);
		}
	}

	AH = OpenArchive(inputFileSpec, opts->format);

	/* Let the archiver know how noisy to be */
	AH->verbose = opts->verbose;

	/*
	 * Whether to keep submitting sql commands as "pg_restore ... | psql ... "
	 */
	AH->exit_on_error = opts->exit_on_error;

	if (opts->tocFile)
		SortTocFromFile(AH, opts);
	else if (opts->noDataForFailedTables)
	{
		/*
		 * we implement this option by clearing idWanted entries, so must
		 * create a dummy idWanted array if there wasn't a tocFile
		 */
		InitDummyWantedList(AH, opts);
	}

	if (opts->tocSummary)
		PrintTOCSummary(AH, opts);
	else
		RestoreArchive(AH, opts);

	/* done, print a summary of ignored errors */
	if (AH->n_errors)
		fprintf(stderr, _("WARNING: errors ignored on restore: %d\n"),
				AH->n_errors);

	/* AH may be freed in CloseArchive? */
	exit_code = AH->n_errors ? 1 : 0;

	CloseArchive(AH);

	return exit_code;
}

static void
usage(const char *progname)
{
	printf(_("%s restores a PostgreSQL database from an archive created by pg_dump.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... [FILE]\n"), progname);

	printf(_("\nGeneral options:\n"));
	printf(_("  -d, --dbname=NAME        connect to database name\n"));
	printf(_("  -f, --file=FILENAME      output file name\n"));
	printf(_("  -F, --format=c|d|t       backup file format (should be automatic)\n"));
	printf(_("  -l, --list               print summarized TOC of the archive\n"));
	printf(_("  -v, --verbose            verbose mode\n"));
	printf(_("  --help                   show this help, then exit\n"));
	printf(_("  --version                output version information, then exit\n"));

	printf(_("\nOptions controlling the restore:\n"));
	printf(_("  -a, --data-only          restore only the data, no schema\n"));
	printf(_("  -c, --clean              clean (drop) database objects before recreating\n"));
	printf(_("  -C, --create             create the target database\n"));
	printf(_("  -e, --exit-on-error      exit on error, default is to continue\n"));
	printf(_("  -I, --index=NAME         restore named index\n"));
	printf(_("  -j, --jobs=NUM           use this many parallel jobs to restore\n"));
	printf(_("  -L, --use-list=FILENAME  use table of contents from this file for\n"
			 "                           selecting/ordering output\n"));
	printf(_("  -n, --schema=NAME        restore only objects in this schema\n"));
	printf(_("  -O, --no-owner           skip restoration of object ownership\n"));
	printf(_("  -P, --function=NAME(args)\n"
			 "                           restore named function\n"));
	printf(_("  -s, --schema-only        restore only the schema, no data\n"));
	printf(_("  -S, --superuser=NAME     superuser user name to use for disabling triggers\n"));
	printf(_("  -t, --table=NAME         restore named table\n"));
	printf(_("  -T, --trigger=NAME       restore named trigger\n"));
	printf(_("  -x, --no-privileges      skip restoration of access privileges (grant/revoke)\n"));
	printf(_("  --disable-triggers       disable triggers during data-only restore\n"));
	printf(_("  --no-data-for-failed-tables\n"
			 "                           do not restore data of tables that could not be\n"
			 "                           created\n"));
	printf(_("  --no-tablespaces         do not restore tablespace assignments\n"));
	printf(_("  --no-security-label      do not restore security labels\n"));
	printf(_("  --role=ROLENAME          do SET ROLE before restore\n"));
	printf(_("  --use-set-session-authorization\n"
			 "                           use SET SESSION AUTHORIZATION commands instead of\n"
	  "                           ALTER OWNER commands to set ownership\n"));
	printf(_("  -1, --single-transaction\n"
			 "                           restore as a single transaction\n"));

	printf(_("\nConnection options:\n"));
	printf(_("  -h, --host=HOSTNAME      database server host or socket directory\n"));
	printf(_("  -p, --port=PORT          database server port number\n"));
	printf(_("  -U, --username=NAME      connect as specified database user\n"));
	printf(_("  -w, --no-password        never prompt for password\n"));
	printf(_("  -W, --password           force password prompt (should happen automatically)\n"));

	printf(_("\nIf no input file name is supplied, then standard input is used.\n\n"));
	printf(_("Report bugs to <pgsql-bugs@postgresql.org>.\n"));
}
