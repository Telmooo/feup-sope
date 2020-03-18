#include <parse.h>

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <utils.h>

int parse_cmd(int argc, char *argv[], struct parse_info_t *info) {
    int flags = 0;

    if (argv == NULL || info == NULL) {
        write(STDERR_FILENO, "Invalid argument, one or more null pointers\n", 44);
        flags |= FLAG_ERR;
        return flags;
    }

    // Obligatory flag -l
    if (strcmp(argv[0], "-l") != 0 && strcmp(argv[0], "--count-links") != 0) {
        write(STDERR_FILENO, "Missing obligatory flag: -l\n", 28);
        flags |= FLAG_ERR;
        return flags;
    }

    flags |= FLAG_LINKS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            if (flags & FLAG_ALL) {
                write(STDERR_FILENO, "Repeated flag: -a or --all\n", 28);
                flags |= FLAG_ERR;
                return flags;
            }

            flags |= FLAG_ALL; // update flag
        }
        else if (strcmp(argv[i], "--bytes") == 0) {
            if (flags & FLAG_BYTES) {
                write(STDERR_FILENO, "Repeated flag: -b or --bytes\n", 30);
                flags |= FLAG_ERR;
                return flags;
            }

            flags |= FLAG_BYTES; // update flag
        }
        else if (strncmp(argv[i], "--block-size=", 13) == 0) {
            if (flags & FLAG_BSIZE) {
                write(STDERR_FILENO, "Repeated flag: -B or --block-size\n", 35);
                flags |= FLAG_ERR;
                return flags;
            }

            char *tmp = argv[i] + 13; // skip "--block-size="

            if (strlen(tmp) == 0 || str_isDigit(tmp) < 1) {
                write(STDERR_FILENO, "Flag -B or --block-size missing an integer\n", 44);
                flags |= FLAG_ERR;
                return flags;
            }

            sscanf(tmp, "%d", &(info->block_size));
            printf("Size: %d\n", (info->block_size));

            flags |= FLAG_BSIZE; // update flag
        }
        else if (strcmp(argv[i], "--dereference") == 0) {
            if (flags & FLAG_DEREF) {
                write(STDERR_FILENO, "Repeated flag: -L or --dereference\n", 36);
                flags |= FLAG_ERR;
                return flags;
            }

            flags |= FLAG_DEREF; // update flag
        }
        else if (strcmp(argv[i], "--separate-dirs") == 0) {
            if (flags & FLAG_DEREF) {
                write(STDERR_FILENO, "Repeated flag: -S or --separate-dirs\n", 38);
                flags |= FLAG_ERR;
                return flags;
            }

            flags |= FLAG_SEPDIR; // update flag
        }
        else if (strncmp(argv[i], "--max-depth=", 12) == 0) {
            if (flags & FLAG_MAXDEPTH) {
                write(STDERR_FILENO, "Repeated flag: --max-depth\n", 38);
                flags |= FLAG_ERR;
                return flags;
            }

            char *tmp = argv[i] + 12; // skip "--max-depth="

            if (strlen(tmp) == 0 || str_isDigit(tmp) < 1) {
                write(STDERR_FILENO, "Flag --max-depth must have an integer\n", 47);
                flags |= FLAG_ERR;
                return flags;
            }

            sscanf(tmp, "%d", &(info->max_depth));
            printf("Max Depth: %d\n", (info->max_depth));

            flags |= FLAG_MAXDEPTH; // update flag
        }
        else if (strncmp(argv[i], "-", 1) == 0) {
            char *tmp = argv[i] + 1; // skip "-"

            if (strlen(tmp) == 0 || str_isAlpha(tmp) < 1) {
                write(STDERR_FILENO, "Invalid flag or flags\n", 23);
                flags |= FLAG_ERR;
                return flags;
            }

            if (str_find(tmp, "l", 0) >= 0) {
                if (flags & FLAG_ALL) {
                    write(STDERR_FILENO, "Repeated flag: -a or --all\n", 28);
                    flags |= FLAG_ERR;
                    return flags;
                }
            }
            if (str_find(tmp, "a", 0) >= 0) {
                if (flags & FLAG_ALL) {
                    write(STDERR_FILENO, "Repeated flag: -a or --all\n", 28);
                    flags |= FLAG_ERR;
                    return flags;
                }

                flags |= FLAG_ALL; // update flag
            }
            if (str_find(tmp, "b", 0) >= 0) {
                if (flags & FLAG_BYTES) {
                    write(STDERR_FILENO, "Repeated flag: -b or --bytes\n", 30);
                    flags |= FLAG_ERR;
                    return flags;
                }

                flags |= FLAG_BYTES; // update flag
            }
            if (str_find(tmp, "L", 0) >= 0) {
                if (flags & FLAG_DEREF) {
                    write(STDERR_FILENO, "Repeated flag: -L or --dereference\n", 36);
                    flags |= FLAG_ERR;
                    return flags;
                }

                flags |= FLAG_DEREF; // update flag
            }
            if (str_find(tmp, "S", 0) >= 0) {
                if (flags & FLAG_SEPDIR) {
                    write(STDERR_FILENO, "Repeated flag: -S or --separate-dirs\n", 38);
                    flags |= FLAG_ERR;
                    return flags;
                }

                flags |= FLAG_SEPDIR; // update flag
            }
            if (str_find(tmp, "B", 0) >= 0) {
                if (flags & FLAG_BSIZE) {
                    write(STDERR_FILENO, "Repeated flag: -B or --block-size\n", 35);
                    flags |= FLAG_ERR;
                    return flags;
                }

                if (i + 1 >= argc || strlen(argv[i+1]) == 0 || str_isDigit(argv[i+1]) < 1) {
                    write(STDERR_FILENO, "Flag -B or --block-size missing an integer\n", 44);
                    flags |= FLAG_ERR;
                    return flags;
                }
                printf("Teste\n");
                sscanf(argv[i+1], "%d", &(info->block_size));
                printf("Size: %d\n", (info->block_size));

                flags |= FLAG_BSIZE; // update flag
                i++;
            }
        }
        else { // verify if it's valid path

            if (flags & FLAG_PATH) {
                write(STDERR_FILENO, "Repeated flag: path\n", 21);
                flags |= FLAG_ERR;
                return flags;
            }

            int r = path_isdir(argv[i]);

            switch (r) {
                case -1:
                    flags |= FLAG_ERR;
                    return flags;
                case 0:
                    write(STDERR_FILENO, "Path isn't a directory\n", 20);
                    flags |= FLAG_ERR;
                    return flags;
                case 1:
                    info->path = strdup(argv[i]);
                    flags |= FLAG_PATH;
            }
        }
    }



    return flags;
}