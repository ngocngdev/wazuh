/* Copyright (C) 2009 Trend Micro Inc.
 * All rights reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#include "manage_agents.h"
#include <stdlib.h>

#if defined(__MINGW32__) || defined(__hppa__)
static int setenv(const char *name, const char *val, __attribute__((unused)) int overwrite)
{
    int len = strlen(name) + strlen(val) + 2;
    char *str = (char *)malloc(len);
    snprintf(str, len, "%s=%s", name, val);
    putenv(str);
    return 0;
}
#endif

__attribute__((noreturn)) static void helpmsg()
{
    print_header();
    print_out("  %s -[Vhlj] [-a <ip> -n <name>] [-F sec] [-e id] [-r id] [-i id] [-f file]", ARGV0);
    print_out("    -V          Version and license message.");
    print_out("    -h          This help message.");
    print_out("    -j          Use JSON output.");
    print_out("    -l          List available agents.");
    print_out("    -L          Disable agents limit.");
    print_out("    -a <ip>     Add new agent.");
    print_out("    -n <name>   Name for new agent.");
    print_out("    -e <id>     Extracts key for an agent (Manager only).");
    print_out("    -r <id>     Remove an agent (Manager only).");
    print_out("    -i <key>    Import authentication key (Agent only).");
    print_out("    -F <sec>    Remove agents with duplicated IP if disconnected since <sec> seconds.");
    print_out("    -f <file>   Bulk generate client keys from file (Manager only).");
    print_out("                <file> contains lines in IP,NAME format.");
    exit(1);
}

static void print_banner()
{
    printf("\n");
    printf(BANNER, __ossec_name, __ossec_version, (int)(21 - strlen(__ossec_name) - strlen(__ossec_version)), "                     ");

#ifdef CLIENT
    printf(BANNER_CLIENT);
#else
    printf(BANNER_OPT);
#endif

    return;
}

#ifndef WIN32
/* Clean shutdown on kill */
__attribute__((noreturn)) void manage_shutdown(__attribute__((unused)) int sig)
{
    printf("\n");
    printf(EXIT);

    exit(0);
}
#endif

char shost[512];

int main(int argc, char **argv)
{
    char *user_msg;
    int c = 0, cmdlist = 0, json_output = 0, no_limit = 0;
    int force_antiquity;
    char *end;
    const char *cmdexport = NULL;
    const char *cmdimport = NULL;
    const char *cmdbulk = NULL;
#ifndef WIN32
    const char *dir = DEFAULTDIR;
    const char *group = GROUPGLOBAL;
    gid_t gid;
#else
    FILE *fp;
    TCHAR path[2048];
    DWORD last_error;
    int ret;
#endif

    /* Set the name */
    OS_SetName(ARGV0);

    while ((c = getopt(argc, argv, "Vhle:r:i:f:ja:n:F:L")) != -1) {
        switch (c) {
            case 'V':
                print_version();
                break;
            case 'h':
                helpmsg();
                break;
            case 'e':
#ifdef CLIENT
                merror_exit("Key export only available on a master.");
#endif
                if (!optarg) {
                    merror_exit("-e needs an argument.");
                }
                cmdexport = optarg;
                break;
            case 'r':
#ifdef CLIENT
                merror_exit("Key removal only available on a master.");
#endif
                if (!optarg) {
                    merror_exit("-r needs an argument.");
                }

                /* Use environment variables already available to remove_agent() */
                setenv("OSSEC_ACTION", "r", 1);
                setenv("OSSEC_AGENT_ID", optarg, 1);
                setenv("OSSEC_ACTION_CONFIRMED", "y", 1);
                break;
            case 'i':
#ifndef CLIENT
                merror_exit("Key import only available on an agent.");
#endif
                if (!optarg) {
                    merror_exit("-i needs an argument.");
                }
                cmdimport = optarg;
                break;
            case 'f':
#ifdef CLIENT
                merror_exit("Bulk generate keys only available on a master.");
#endif
                if (!optarg) {
                    merror_exit("-f needs an argument.");
                }
                cmdbulk = optarg;
                printf("Bulk load file: %s\n", cmdbulk);
                break;
            case 'l':
                cmdlist = 1;
                break;
            case 'j':
                json_output = 1;
                break;
            case 'a':
#ifdef CLIENT
                merror_exit("Agent adding only available on a master.");
#endif
                if (!optarg)
                    merror_exit("-a needs an argument.");
                setenv("OSSEC_ACTION", "a", 1);
                setenv("OSSEC_ACTION_CONFIRMED", "y", 1);
                setenv("OSSEC_AGENT_IP", optarg, 1);
                setenv("OSSEC_AGENT_ID", "0", 1);
            break;
            case 'n':
                if (!optarg)
                    merror_exit("-n needs an argument.");
                setenv("OSSEC_AGENT_NAME", optarg, 1);
                break;
            case 'F':
                if (!optarg)
                    merror_exit("-F needs an argument.");

                force_antiquity = strtol(optarg, &end, 10);

                if (optarg == end || force_antiquity < 0)
                    merror_exit("Invalid number for -F");

                setenv("OSSEC_REMOVE_DUPLICATED", optarg, 1);
                break;
            case 'L':
                no_limit = 1;
                break;
            default:
                helpmsg();
                break;
        }
    }

    /* Get current time */
    time1 = time(0);

    /* Before chroot */
    srandom_init();
    getuname();

#ifndef CLIENT
    int is_worker = w_is_worker();
    char *master;

    switch (is_worker) {
        case -1:
            merror("Invalid option at cluster configuration");
            return 0;
        case 1:
            master = get_master_node();
            merror("Wazuh is running in cluster mode: %s is not available in worker nodes. Please, try again in the master node: %s.", ARGV0, master);
            free(master);
            return 0;
    }
#endif

#ifndef WIN32
    if (gethostname(shost, sizeof(shost) - 1) < 0) {
        strncpy(shost, "localhost", sizeof(shost) - 1);
        shost[sizeof(shost) - 1] = '\0';
    }

    /* Get the group name */
    gid = Privsep_GetGroup(group);
    if (gid == (gid_t) - 1) {
        merror_exit(USER_ERROR, "", group);
    }

    /* Set the group */
    if (Privsep_SetGroup(gid) < 0) {
        merror_exit(SETGID_ERROR, group, errno, strerror(errno));
    }

    /* Load ossec uid and gid for creating backups */
    if (OS_LoadUid() < 0) {
        merror_exit("Couldn't get user and group id.");
    }

    /* Chroot to the default directory */
    if (Privsep_Chroot(dir) < 0) {
        merror_exit(CHROOT_ERROR, dir, errno, strerror(errno));
    }

    /* Inside chroot now */
    nowChroot();

    /* Start signal handler */
    StartSIG2(ARGV0, manage_shutdown);
#else
    /* Get full path to the directory this executable lives in */
    ret = GetModuleFileName(NULL, path, sizeof(path));

    /* Check for errors */
    if (!ret) {
        merror_exit(GMF_ERROR);
    }

    /* Get last error */
    last_error = GetLastError();

    /* Look for errors */
    if (last_error != ERROR_SUCCESS) {
        if (last_error == ERROR_INSUFFICIENT_BUFFER) {
            merror_exit(GMF_BUFF_ERROR, ret, sizeof(path));
        } else {
            merror_exit(GMF_UNKN_ERROR, last_error);
        }
    }

    /* Remove file name from path */
    PathRemoveFileSpec(path);

    /* Move to correct directory */
    if (chdir(path)) {
        merror_exit(CHDIR_ERROR, path, errno, strerror(errno));
    }

    /* Check permissions */
    fp = fopen(OSSECCONF, "r");
    if (fp) {
        fclose(fp);
    } else {
        merror_exit(CONF_ERROR, OSSECCONF);
    }
#endif

    if (cmdlist == 1) {
        list_agents(cmdlist);
        exit(0);
    } else if (cmdimport) {
        k_import(cmdimport);
        exit(0);
    } else if (cmdexport) {
        k_extract(cmdexport, json_output);
        exit(0);
    } else if (cmdbulk) {
        k_bulkload(cmdbulk);
        exit(0);
    }

    /* Little shell */
    while (1) {
        int leave_s = 0;

        if (!json_output)
            print_banner();

        /* Get ACTION from the environment. If ACTION is specified,
         * we must set leave_s = 1 to ensure that the loop will end */
        user_msg = getenv("OSSEC_ACTION");
        if (user_msg == NULL) {
            user_msg = read_from_user();
        } else {
            leave_s = 1;
        }

        /* All the allowed actions */
        switch (user_msg[0]) {
            case 'A':
            case 'a':
#ifdef CLIENT
                printf("\n ** Agent adding only available on a master ** \n\n");
                break;
#endif
                add_agent(json_output, no_limit);
                break;
            case 'e':
            case 'E':
#ifdef CLIENT
                printf("\n ** Key export only available on a master ** \n\n");
                break;
#endif
                k_extract(NULL, json_output);
                break;
            case 'i':
            case 'I':
#ifdef CLIENT
                k_import(NULL);
#else
                printf("\n ** Key import only available on an agent ** \n\n");
#endif
                break;
            case 'l':
            case 'L':
                list_agents(0);
                break;
            case 'r':
            case 'R':
#ifdef CLIENT
                printf("\n ** Key removal only available on a master ** \n\n");
                break;
#endif
                remove_agent(json_output);
                break;
            case 'q':
            case 'Q':
                leave_s = 1;
                break;
            case 'V':
                print_version();
                break;
            default:
                printf("\n ** Invalid Action ** \n\n");
                break;
        }

        if (leave_s) {
            break;
        }

        continue;
    }

    if (!json_output) {
        printf("\n");
        printf(EXIT);
    }

    return (0);
}
