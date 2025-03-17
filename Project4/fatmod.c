#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/msdos_fs.h>

#define SECTORSIZE 512
#define CLUSTERSIZE  1024
#define MAX_FILENAME_LEN 12
#define ATTR_VOLUME_ID 0x08

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
void list_directory_contents(int fd, struct fat_boot_sector *bs);
void read_ascii_file(int fd, struct fat_boot_sector *bs, const char *filename);
void read_file_binary(int fd, struct fat_boot_sector *bs, const char *filename);
void delete_file(int fd, struct fat_boot_sector *bs, const char *filename);
void create_file(int fd, struct fat_boot_sector *bs, const char *filename);
unsigned int allocateCluster(int fd, struct fat_boot_sector *bs, unsigned int last_cluster);
unsigned int getNextCluster(int fd, struct fat_boot_sector *bs, unsigned int current_cluster);
void write_file(int fd, struct fat_boot_sector *bs, const char *filename, unsigned int offset, unsigned int n, unsigned char data);
void print_help();

void convert_to_uppercase(char *str);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        if (strcmp(argv[1], "-h") == 0) {
            print_help();
            exit(EXIT_SUCCESS);
        }
        fprintf(stderr, "Invalid option, Type ./fatmod -h for a help menu\n");
        exit(EXIT_FAILURE);
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Failed to open disk image");
        exit(EXIT_FAILURE);
    }

    unsigned char sector[SECTORSIZE];
    if (readsector(fd, sector, 0) != 0) {
        fprintf(stderr, "Error reading boot sector\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    struct fat_boot_sector *bs = (struct fat_boot_sector *)sector;

    if (strcmp(argv[2], "-l") == 0) {
        list_directory_contents(fd, bs);
    } else if (strcmp(argv[2], "-r") == 0 && argc == 5) {
        if (strcmp(argv[3], "-a") == 0) {
            read_ascii_file(fd, bs, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            read_file_binary(fd, bs, argv[4]);
        }
        else {
            fprintf(stderr, "Invalid option, Type ./fatmod -h for a help menu\n");
            close(fd);
            exit(EXIT_FAILURE);
        }
    } else if (strcmp(argv[2], "-c") == 0 && argc == 4) {
        create_file(fd, bs, argv[3]);
    } else if (strcmp(argv[2], "-d") == 0 && argc == 4) {
        delete_file(fd, bs, argv[3]);
    } else if (strcmp(argv[2], "-w") == 0 && argc == 7) {
        write_file(fd, bs, argv[3], atoi(argv[4]), atoi(argv[5]), (unsigned char)atoi(argv[6]));
    }  else {
        fprintf(stderr, "Invalid option, Type ./fatmod -h for a help menu\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
    return 0;
}



void format_filename(char *dest, const char *src) {
    int i, j;

    for (i = 0; i < 8 && src[i] != ' '; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';

    if (src[8] != ' ') {
        dest[i++] = '.';

        for (j = 8; j < 11 && src[j] != ' '; j++, i++) {
            dest[i] = src[j];
        }
        dest[i] = '\0';
    }
}

void list_directory_contents(int fd, struct fat_boot_sector *bs) {
    unsigned char cluster[CLUSTERSIZE];
    int cluster_offset = bs->reserved + (bs->fats * bs->fat32.length);

    if (readsector(fd, cluster, cluster_offset) != 0) {
        fprintf(stderr, "Failed to read root directory cluster\n");
        return;
    }

    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    char filename[MAX_FILENAME_LEN];

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] == 0x00) {
            break;
        }
        if (entry[i].name[0] == 0xE5 || (entry[i].attr & ATTR_VOLUME_ID)) {
            continue;
        }

        format_filename(filename, (char *)entry[i].name);
        printf("%s %u\n", filename, entry[i].size);
    }
}

void read_cluster(int fd, unsigned char *buf, unsigned int cluster_number, struct fat_boot_sector *bs) {
    unsigned int sector_number = bs->reserved + bs->fats * bs->fat32.length + (cluster_number - 2) * bs->sec_per_clus;
    for (int i = 0; i < bs->sec_per_clus; i++) {
        if (readsector(fd, buf + (i * SECTORSIZE), sector_number + i) != 0) {
            fprintf(stderr, "Failed to read sector %u\n", sector_number + i);
            return;
        }
    }
}

void read_ascii_file(int fd, struct fat_boot_sector *bs, const char *filename) {
    unsigned char cluster[CLUSTERSIZE];
    int root_dir_sector = bs->reserved + (bs->fats * bs->fat32.length);
    if (readsector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to read root directory sector\n");
        return;
    }

    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    char file_name[MAX_FILENAME_LEN];

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] == 0x00) {
            break;
        }
        if (entry[i].name[0] == 0xE5 || (entry[i].attr & ATTR_VOLUME_ID)) {
            continue;
        }

        format_filename(file_name, (char *)entry[i].name);

        if (strcasecmp(file_name, filename) == 0) {
            unsigned int cluster_number = entry[i].starthi << 16 | entry[i].start;
            unsigned int file_size = entry[i].size;

            printf("Reading file: %s, Size: %u\n", file_name, file_size);

            while (file_size > 0 && cluster_number < 0x0FFFFFF8) {
                memset(cluster, 0, CLUSTERSIZE);
                read_cluster(fd, cluster, cluster_number, bs);

                int bytes_to_read = file_size < CLUSTERSIZE ? file_size : CLUSTERSIZE;
                fwrite(cluster, 1, bytes_to_read, stdout);
                file_size -= bytes_to_read;

                if (file_size > 0) {
                    unsigned int fat_offset = bs->reserved * SECTORSIZE + cluster_number * 4;
                    lseek(fd, fat_offset, SEEK_SET);
                    read(fd, &cluster_number, sizeof(cluster_number));
                    cluster_number = cluster_number & 0x0FFFFFFF;

                    if (cluster_number >= 0x0FFFFFF8) {
                        break;
                    }
                }
            }
            printf("\n");
            return;
        }
    }
    fprintf(stderr, "File not found\n");
}

void read_file_binary(int fd, struct fat_boot_sector *bs, const char *filename) {
    unsigned char cluster[CLUSTERSIZE];
    int cluster_offset = bs->reserved + (bs->fats * bs->fat32.length);
    if (readsector(fd, cluster, cluster_offset) != 0) {
        fprintf(stderr, "Failed to read root directory cluster\n");
        return;
    }

    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    char file_name[MAX_FILENAME_LEN];

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] == 0x00) {
            break;
        }
        if (entry[i].name[0] == 0xE5 || (entry[i].attr & ATTR_VOLUME_ID)) {
            continue;
        }

        format_filename(file_name, (char *)entry[i].name);

        if (strcasecmp(file_name, filename) == 0) {
            unsigned int cluster_number = entry[i].starthi << 16 | entry[i].start;
            unsigned int file_size = entry[i].size;

            unsigned int offset = 0;
            while (file_size > 0 && cluster_number < 0x0FFFFFF8) {
                read_cluster(fd, cluster, cluster_number, bs);

                int bytes_to_read = file_size < CLUSTERSIZE ? file_size : CLUSTERSIZE;
                for (int j = 0; j < bytes_to_read; j++, file_size--, offset++) {
                    if (offset % 16 == 0) {
                        if (offset > 0) {
                            printf("\n");
                        }
                        printf("%08x: ", offset);
                    }
                    printf("%02x ", cluster[j]);
                }

                if (file_size > 0) {
                    unsigned int fat_offset = bs->reserved * SECTORSIZE + cluster_number * 4;
                    lseek(fd, fat_offset, SEEK_SET);
                    read(fd, &cluster_number, sizeof(cluster_number));
                    cluster_number = cluster_number & 0x0FFFFFFF;
                } else {
                    break;
                }
            }
            printf("\n");
            return;
        }
    }
    fprintf(stderr, "File not found\n");
}

void create_file(int fd, struct fat_boot_sector *bs, const char *filename) {

    const char *dot = strrchr(filename, '.');

    if (dot != NULL) {
        const char *extension = dot + 1;
        size_t ext_length = strlen(extension);

        if (ext_length > 3) {
            printf("Invalid extension: \"%s\". It must be at most 3 characters long.\n", extension);
            return;
        }
    }

    unsigned char cluster[CLUSTERSIZE];
    int root_dir_sector = bs->reserved + (bs->fats * bs->fat32.length);
    if (readsector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to read root directory sector\n");
        return;
    }

    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    struct msdos_dir_entry *free_entry = NULL;

    char fat_filename[MSDOS_NAME + 1];
    memset(fat_filename, ' ', MSDOS_NAME);
    for (int i = 0, j = 0; filename[j] != '\0' && i < MSDOS_NAME; i++, j++) {
        if (filename[j] == '.') {
            i = 7;
            continue;
        }
        fat_filename[i] = toupper((unsigned char)filename[j]);
    }
    fat_filename[MSDOS_NAME] = '\0';

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] != 0x00 && entry[i].name[0] != 0xE5) {
            char existing_filename[MSDOS_NAME + 1];
            memset(existing_filename, ' ', MSDOS_NAME);
            memcpy(existing_filename, entry[i].name, MSDOS_NAME);
            existing_filename[MSDOS_NAME] = '\0';

            if (strncmp(fat_filename, existing_filename, MSDOS_NAME) == 0) {
                fprintf(stderr, "File '%s' already exists\n", filename);
                return;
            }
        }
    }

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] == 0x00 || entry[i].name[0] == 0xE5) {
            free_entry = &entry[i];
            break;
        }
    }

    if (!free_entry) {
        fprintf(stderr, "No space in root directory\n");
        return;
    }

    unsigned int new_cluster = allocateCluster(fd, bs,0);
    if (new_cluster == 0) {
        fprintf(stderr, "No free cluster available\n");
        return;
    }

    memcpy(free_entry->name, fat_filename, MSDOS_NAME);
    free_entry->attr = 0x20;
    free_entry->size = 0;
    free_entry->start = new_cluster & 0xFFFF;
    free_entry->starthi = (new_cluster >> 16) & 0xFFFF;

    if (writesector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to write updated directory sector\n");
        return;
    }

    printf("File '%s' created successfully.\n", filename);
}
void write_file(int fd, struct fat_boot_sector *bs, const char *filename, unsigned int offset, unsigned int n, unsigned char data) {

    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    int root_dir_sector = bs->reserved + (bs->fats * bs->fat32.length);


    if (readsector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to read root directory sector\n");
        return;
    }


    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    char file_name[MAX_FILENAME_LEN];
    struct msdos_dir_entry *file_entry = NULL;

    for (int i = 0; i < entries_per_cluster; i++) {
        format_filename(file_name, (char *)entry[i].name);
        if (!(entry[i].attr & ATTR_VOLUME_ID) && strcasecmp(file_name, filename) == 0) {
            file_entry = &entry[i];
            break;
        }
    }

    if (!file_entry) {
        fprintf(stderr, "File '%s' not found\n", filename);
        return;
    }

    unsigned int file_size = file_entry->size;
    unsigned int end_position = offset + n;

    if(offset > file_size){
        fprintf(stderr, "Error: The offset value exceeds the file size. The current file size is '%d'\n", file_size);
        return;
    }

    if (end_position > file_size) {
        file_size = end_position;
        file_entry->size = file_size;
    }

    unsigned int cluster_number = file_entry->starthi << 16 | file_entry->start;
    unsigned int cluster_index = offset / CLUSTERSIZE;
    unsigned int cluster_offset = offset % CLUSTERSIZE;
    unsigned int sector_number;

    while (cluster_index > 0) {
        cluster_number = getNextCluster(fd, bs, cluster_number);
        if (cluster_number >= 0x0FFFFFF8) {
            cluster_number = allocateCluster(fd, bs, cluster_number);
            if (cluster_number == 0xFFFFFFFF) {
                fprintf(stderr, "Failed to allocate new cluster\n");
                return;
            }
        }
        cluster_index--;
    }
    while (n > 0) {

        unsigned int sector_index = cluster_offset / SECTORSIZE;
        unsigned int sector_offset = cluster_offset % SECTORSIZE;

        unsigned int write_size = (n < (SECTORSIZE - sector_offset)) ? n : (SECTORSIZE - sector_offset);

        sector_number = bs->reserved + (bs->fats * bs->fat32.length) + (cluster_number - 2) * bs->sec_per_clus + sector_index;
        if (readsector(fd, sector, sector_number) != 0) {
            fprintf(stderr, "Failed to read sector %u\n", sector_number);
            return;
        }

        for (unsigned int i = 0; i < write_size; i++) {
            sector[sector_offset + i] = data;
        }

        if (writesector(fd, sector, sector_number) != 0) {
            fprintf(stderr, "Failed to write sector %u\n", sector_number);
            return;
        }

        n -= write_size;
        cluster_offset += write_size;

        if (cluster_offset >= CLUSTERSIZE) {
            cluster_offset = 0;
            unsigned int next_cluster = getNextCluster(fd, bs, cluster_number);
            if (next_cluster >= 0x0FFFFFF8) {
                next_cluster = allocateCluster(fd, bs, cluster_number);
                if (next_cluster == 0xFFFFFFFF) {
                    fprintf(stderr, "Failed to allocate new cluster\n");
                    return;
                }
                cluster_number = next_cluster;
            } else {
                cluster_number = next_cluster;
            }
        }
    }

    if (writesector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to write updated directory sector\n");
        return;
    }

    printf("Data written successfully.\n");
}

unsigned int allocateCluster(int fd, struct fat_boot_sector *bs, unsigned int last_cluster) {
    unsigned int total_clusters = (bs->total_sect - bs->reserved - bs->fats * bs->fat32.length) / bs->sec_per_clus;

    unsigned int fat_entry;
    unsigned int new_cluster = 0;
    off_t fat_offset;

    for (new_cluster = 2; new_cluster < total_clusters; new_cluster++) {
        fat_offset = bs->reserved * SECTORSIZE + new_cluster * 4;
        if (lseek(fd, fat_offset, SEEK_SET) != fat_offset) {
            fprintf(stderr, "Error seeking in FAT\n");
            return 0xFFFFFFFF;
        }
        if (read(fd, &fat_entry, sizeof(fat_entry)) != sizeof(fat_entry)) {
            fprintf(stderr, "Error reading FAT\n");
            return 0xFFFFFFFF;
        }
        if ((fat_entry & 0x0FFFFFFF) == 0) {
            break;
        }
    }

    if (new_cluster == total_clusters) {
        fprintf(stderr, "No free clusters available\n");
        return 0xFFFFFFFF;
    }

    fat_entry = 0x0FFFFFFF;
    if (lseek(fd, fat_offset, SEEK_SET) != fat_offset) {
        fprintf(stderr, "Error seeking in FAT to write\n");
        return 0xFFFFFFFF;
    }
    if (write(fd, &fat_entry, sizeof(fat_entry)) != sizeof(fat_entry)) {
        fprintf(stderr, "Error writing to FAT\n");
        return 0xFFFFFFFF;
    }

    if (last_cluster != 0) {
        fat_offset = bs->reserved * SECTORSIZE + last_cluster * 4;
        if (lseek(fd, fat_offset, SEEK_SET) != fat_offset) {
            fprintf(stderr, "Error seeking in FAT to update last cluster\n");
            return 0xFFFFFFFF;
        }
        if (write(fd, &new_cluster, sizeof(new_cluster)) != sizeof(new_cluster)) {
            fprintf(stderr, "Error updating last cluster in FAT\n");
            return 0xFFFFFFFF;
        }
    }

    return new_cluster;
}

unsigned int getNextCluster(int fd, struct fat_boot_sector *bs, unsigned int current_cluster) {
    unsigned int next_cluster;
    off_t fat_offset = bs->reserved * SECTORSIZE + current_cluster * 4;
    lseek(fd, fat_offset, SEEK_SET);
    read(fd, &next_cluster, sizeof(next_cluster));
    next_cluster &= 0x0FFFFFFF;
    return next_cluster;
}






void delete_file(int fd, struct fat_boot_sector *bs, const char *filename) {
    unsigned char cluster[CLUSTERSIZE];
    int root_dir_sector = bs->reserved + (bs->fats * bs->fat32.length);
    if (readsector(fd, cluster, root_dir_sector) != 0) {
        fprintf(stderr, "Failed to read root directory sector\n");
        return;
    }

    struct msdos_dir_entry *entry = (struct msdos_dir_entry *)cluster;
    int entries_per_cluster = CLUSTERSIZE / sizeof(struct msdos_dir_entry);
    char file_name[MAX_FILENAME_LEN];

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entry[i].name[0] == 0x00) {
            break;
        }
        format_filename(file_name, (char *)entry[i].name);

        if (!(entry[i].attr & ATTR_VOLUME_ID) && strcasecmp(file_name, filename) == 0) {
            entry[i].name[0] = 0xE5;

            if (writesector(fd, cluster, root_dir_sector) != 0) {
                fprintf(stderr, "Failed to write updated directory sector\n");
                return;
            }
            unsigned int cluster_number = entry[i].starthi << 16 | entry[i].start;
            while (cluster_number < 0x0FFFFFF8) {
                unsigned int next_cluster;
                off_t fat_offset = bs->reserved * SECTORSIZE + cluster_number * 4;
                lseek(fd, fat_offset, SEEK_SET);
                read(fd, &next_cluster, sizeof(next_cluster));
                next_cluster &= 0x0FFFFFFF;

                if (next_cluster >= 0x0FFFFFF8) {
                    unsigned int free_cluster = 0x00000000;
                    lseek(fd, fat_offset, SEEK_SET);
                    write(fd, &free_cluster, sizeof(free_cluster));
                    break; 
                }

                unsigned int free_cluster = 0x00000000;
                lseek(fd, fat_offset, SEEK_SET);
                write(fd, &free_cluster, sizeof(free_cluster));

                cluster_number = next_cluster;
            }

            printf("File '%s' deleted successfully.\n", filename);
            return;
        }
    }

    fprintf(stderr, "File '%s' not found.\n", filename);
}

void print_help() {
    printf("Usage: ./fatmod <disk_image> <option> [<additional_arguments>]\n");
    printf("Options:\n");
    printf("  -l                           List all files in the root directory with their sizes.\n");
    printf("  -r -a <filename>             Read a file in ASCII format and display its contents.\n");
    printf("  -r -b <filename>             Read a file in binary format and display its contents as hexadecimal.\n");
    printf("  -c <filename>                Create a new file with the specified name in the root directory.\n");
    printf("  -d <filename>                Delete a file with the specified name and free its space.\n");
    printf("  -w <filename> <offset> <n> <data>\n");
    printf("                               Write 'n' bytes of 'data' starting from 'offset' into the file.\n");
    printf("  -h                           Display this help information.\n");
}



int readsector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset = snum * SECTORSIZE;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        return -1;
    }
    if (read(fd, buf, SECTORSIZE) != SECTORSIZE) {
        return -1;
    }
    return 0;
}

int writesector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset = snum * SECTORSIZE;
    if (lseek(fd, offset, SEEK_SET) != offset) {
        return -1;
    }
    if (write(fd, buf, SECTORSIZE) != SECTORSIZE) {
        return -1;
    }
    fsync(fd);
    return 0;
}