/*
 * work in progress project to decode information from sc2replay files.
 *
 * Still a lot to do. Currently doesn't provide any way to dealloc any of the
 * memory that is malloc'd durring data block parsing.
 */
#include <mpq.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "parse.h"

/*
 * Read next byte from buffer
 */
char next(struct info *mpq)
{
    mpq->prev = mpq->cur;
    mpq->cur = mpq->buf[mpq->offset++];
    return mpq->cur;
}

/*
 * Peek at next byte without updating offset
 */
char peek(struct info *mpq)
{
    return mpq->buf[mpq->offset];
}

/*
 * Peek at byte i bytes ahead without updating offset
 */
char peek_ahead(struct info *mpq, int i)
{
    return mpq->buf[mpq->offset + i];
}

/*
 * Peek at the next two bytes without updating offset
 */
short peek_short(struct info *mpq)
{
    return *(short *)(&mpq->buf[mpq->offset]);
}

/*
 * Read 4 bytes from buffer
 */
int next_int(struct info *mpq)
{
    int data = *(int *)(&mpq->buf[mpq->offset]);
    mpq->offset += 4;
    mpq->prev = (data & 0xFF00) >> 8;
    mpq->cur = (data & 0xFF);
    return data;
}

/*
 * Read 2 bytes from buffer
 */
short next_short(struct info *mpq)
{
    short data = *(short *)(&mpq->buf[mpq->offset]);
    mpq->offset += 2;
    mpq->prev = (data & 0xFF00) >> 4;
    mpq->cur = (data & 0xFF);
    return data;
}

/* 
   Decode Self-Delimiting Numeric Value
   Slightly modified from reference algo at:
   https://tools.ietf.org/html/draft-irtf-dtnrg-sdnv-07
   Differences:
       - Replays contain encoded bytes in opposite byte order
    - Replays encode signedness by left shifting input value
      and setting right most bit to 0/1 to indicate sign.
 */
int64_t decode_sdnv(struct info *mpq)
{
    int m=sizeof(int64_t)+1, i, g;
    char v;
    int64_t n = 0;
    for (i = 0; i < m; ++i) {
        v = next(mpq);
        /* shift things around to handle byte ordering */
        n |= (v & 0x7F) << i*7;
        if (v >> 7 == 0)
            break;
        else if (i == m-1)
            return -1;
    }
    /* support negative values */
    g = ((n & 1) != 0);
    n >>= 1;
    return (g) ? -n : n;
}

/*
 * Helper to poop out enough tabs when dumping datablocks
 */
void print_depth(int depth)
{
    int i;
    for(i = 0; i < depth; ++i) printf("\t");
}

/*
 * Read and parse a datablock.
 */
void read_data(struct info *mpq, struct data_block *data)
{
    int size, i;
    struct data_block *new;
    char *cp;
    struct data_block **dbp;
    struct data_dict *dmp;
    char a, b;
    short subtype;
    static int prev_type = 0;

    data->type = next(mpq);
    switch (data->type) {
        case TYPE_BIN:
            data->size = decode_sdnv(mpq);
            data->data = malloc((sizeof(char) * data->size)+1);
            cp = data->data;
            for (i = 0; i < data->size; ++i) {
                cp[i] = next(mpq);
            }
            cp[data->size] = '\0';
            break;

        case TYPE_ARRAY:
            /*
             * Array types have bytes 0x10 and 0x00 following the type byte.
             * Unsure what they represent at this point.
             */
            a = next(mpq); 
            b = next(mpq);
            if (a != 1 || b != 0) {
                printf("Array 'sub-type' bytes are invalid (0x%x%x)'\n", a, b);
                break;
            }
            data->size = decode_sdnv(mpq);
            data->data = malloc(sizeof(struct data_block *) * data->size);
            dbp = data->data;
            for (i = 0; i < data->size; ++i) {
                new = malloc(sizeof(struct data_block));
                read_data(mpq, new);
                dbp[i] = new;
            }
            break;

        case TYPE_DICT:
            data->size = decode_sdnv(mpq);
            data->data = malloc(sizeof(struct data_dict));
            dmp = data->data;
            dmp->keys = malloc(sizeof(int) * data->size);
            dmp->values = malloc(sizeof(struct data_block *) * data->size);
            for (i = 0; i < data->size; ++i) {
                new = malloc(sizeof(struct data_block));
                dmp->keys[i] = decode_sdnv(mpq);
                read_data(mpq, new);
                dmp->values[i] = new;
            }
            break;

        case TYPE_BYTE:
            data->size = 1;
            data->data = malloc(1);
            *(char *)data->data = next(mpq);
            break;

        case TYPE_INT:
            data->size = 4;
            data->data = malloc(4);
            *(int *)data->data = next_int(mpq);
            break;

        case TYPE_SDNV:
            data->data = malloc(sizeof(long));
            *(long *)data->data = decode_sdnv(mpq);
            break;

        default:
            printf("Unknown data type: %x (peek: %x) @ %x (%x)\n", data->type, peek(mpq), mpq->offset, prev_type);
            break;
    }
    prev_type = data->type;
}

/*
 * Print binary data blob.
 * If byte is within printable ascii range print that otherwise print a '.'.
 */
void print_binary(int size, char *data)
{
    int i;
    printf("Binary: ");
    for (i = 0; i < size; ++i) {
        if (data[i] >= 0x21 && data[i] <= 0x7E)
            printf("%c", data[i]);
        else
            printf(".");
    }
}

/*
 * Print a "pretty" representation of a decoded data block
 */
void print_data(struct data_block *data)
{
    int i;
    static int depth = 0;

    switch (data->type) {
        case TYPE_BIN:
            print_depth(depth);
            print_binary(data->size, data->data);
            printf("\n");
            break;

        case TYPE_ARRAY:
            print_depth(depth);
            printf("Array[%d] (\n", data->size);
            ++depth;
            for (i = 0; i < data->size; ++i) {
                print_depth(depth);
                printf("[%d] => \n", i);
                print_data(((struct data_block **)data->data)[i]);
            }
            print_depth(depth);
            printf(")\n");
            --depth;
            break;

        case TYPE_DICT:
            print_depth(depth);
            printf("Dict (\n");
            ++depth;
            for (i = 0; i < data->size; ++i) {
                print_depth(depth);
                printf("[%d] => \n", ((struct data_dict *)data->data)->keys[i]);
                ++depth;
                print_data(((struct data_dict *)data->data)->values[i]);
                --depth;
            }
            print_depth(depth);
            printf(")\n");
            --depth;
            break;

        case TYPE_BYTE:
            print_depth(depth);
            printf("Byte: %u\n", *(uint8_t *)data->data);
            break;

        case TYPE_INT:
            print_depth(depth);
            printf("Int: %u\n", *(int *)data->data);
            break;

        case TYPE_SDNV:
            print_depth(depth);
            printf("SDNV: %lu\n", *(long *)data->data);
            break;

        default:
            print_depth(depth);
            printf("Unknown Type\n");
            break;
    }
}

/*
 * Get File number for mpq file
 */
int get_fileno(struct info *mpq, char *file)
{
    int fn;
    libmpq__file_number(mpq->archive, file, &fn);
    return fn;
}

/*
 * Use libmpq to read info file from replay archive
 */
int load_mpq_info(struct info *mpq, char *ifile, char *file)
{
    int fd;
    mpq->offset = 0;
    libmpq__archive_open(&mpq->archive, file, -1);
    fd = get_fileno(mpq, ifile);
    libmpq__file_unpacked_size(mpq->archive, fd, &mpq->size);
    if (mpq->size < 1)
        return 0;
    mpq->buf = malloc(mpq->size);
    libmpq__file_read(mpq->archive, fd, mpq->buf, mpq->size, NULL);
    libmpq__archive_close(mpq->archive);
    return 1;
}

/*
 * Fetch data block from a dict by key
 */
struct data_block *get_dict_index(struct data_block *dict, int index)
{
    return ((struct data_dict *)dict->data)->values[index];
}

/*
 * Fetch data block from an array by index
 */
struct data_block *get_array_index(struct data_block *arr, int index)
{
    return ((struct data_block **)arr->data)[index];
}

/*
 * Load map data
 * NOTE: Currently prints out the map name as well for testing
 */
void load_map(struct data_block *data, struct map *map)
{
    struct data_block *cur, *cur2;
    cur = get_dict_index(data, 1);

    // get name
    map->name = malloc(sizeof(char) * cur->size + 1);
    map->name[cur->size+1] = '\0';
    strncpy(map->name, cur->data, cur->size);
    printf("%s\n", map->name);

}

/*
 * Load data from replay header
 * NOTE: Currently prints out the header data as well for testing
 */
void load_header(struct data_block *data, struct header *hdr)
{
    struct data_block *cur = NULL, *cur2 = NULL;

    // Get version
    cur = get_dict_index(data, 1);
    cur2 = get_dict_index(cur, 1);
    hdr->major = *(int *)cur2->data;
    cur2 = get_dict_index(cur, 2);
    hdr->minor = *(int *)cur2->data;
    cur2 = get_dict_index(cur, 3);
    hdr->rev = *(int *)cur2->data;
    cur2 = get_dict_index(cur, 4);
    hdr->build = *(int *)cur2->data;

    // Get durration
    cur = get_dict_index(data, 3);
    hdr->durration = (*(int *)cur->data) >> 4;

    printf("%d.%d.%d.%d\n", hdr->major, hdr->minor, hdr->rev, hdr->build);
    int hours = hdr->durration / 3600;
    int minutes = (hdr->durration / 60) - (hours * 60);
    int seconds = (hdr->durration % 60);
    printf("%02d:%02d:%02d\n", hours, minutes, seconds);
}

/*
 * Load player data
 * NOTE: Currently prints out the player data as well for testing
 */
void load_players(struct data_block *data, struct player **players[])
{
    struct player *pp = NULL;
    struct player **parr = NULL;
    struct data_block *pa = NULL, *dp = NULL, *player_array, *cur = NULL, *cur2 = NULL;
    int nplayers = 0, i;
    
    player_array = get_dict_index(data, 0);
    nplayers = player_array->size;
    parr = malloc(sizeof(struct player *) * nplayers);

    for (i = 0; i < nplayers; ++i) {
        pp = malloc(sizeof(struct player));
        pa = get_array_index(player_array, i);

        // get name
        cur = get_dict_index(pa, 0);
        pp->name = malloc(sizeof(char) * cur->size + 1);
        pp->name[cur->size+1] = '\0';
        strncpy(pp->name, cur->data, cur->size);

        // get race
        cur = get_dict_index(pa, 2);
        pp->race = malloc(sizeof(char) * cur->size + 1);
        pp->race[cur->size+1] = '\0';
        strncpy(pp->race, cur->data, cur->size);

        // get color
        cur = get_dict_index(pa, 3);
        cur2 = get_dict_index(cur, 0);
        pp->color[0] = *(int *)cur2->data; // R
        cur2 = get_dict_index(cur, 1);
        pp->color[1] = *(int *)cur2->data; // G
        cur2 = get_dict_index(cur, 2);
        pp->color[2] = *(int *)cur2->data; // B
        cur2 = get_dict_index(cur, 3);
        pp->color[3] = *(int *)cur2->data; // Alpha ??

        // get team
        cur = get_dict_index(pa, 5);
        pp->team = *(int *)cur->data;

        printf("%s:%s:#%02x%02x%02x:%d\n", pp->name, pp->race, pp->color[0], pp->color[1], pp->color[2], pp->team);
        parr[i] = pp;
    }

    *players = parr;

}

void parse_messages(struct info *mpq, struct player **players)
{
    int offset, i, t, timestamp = 0;
    char *message, player, channel, length;

    /*mpq->offset = 0x149;*/
    while((peek_ahead(mpq, 2) & 0xFF) == 0x80) {
        mpq->offset += 7;
    }

    while(mpq->offset < mpq->size) {
        t = next(mpq) & 0xFF;
        offset = t >> 2;
        t &= 0x3;
        if (t == 1) {
            offset = (offset << 8) | (next(mpq) & 0xFF);
        } else if (t == 2) {
            offset = (offset << 16) | (next(mpq) & 0xFF);
            offset = (offset << 8) | (next(mpq) & 0xFF);
        }
        timestamp += offset;

        player = (next(mpq) & 0xFF) - 1;
        channel = next(mpq) & 0xFF;
        if ((channel & 0xFF) == 0x83) {
            mpq->offset += 8;
            printf("%x:%s:%d:%d:<PING>\n", timestamp, players[player]->name, channel, length);
        } else {
            length = next(mpq) & 0xFF;
            if ((channel & 0x08)) {
                length += 64;
            }
            message = malloc(sizeof(char) * (length + 1));
            message[length] = '\0';
            for (i = 0; i < length; ++i) {
                message[i] = next(mpq);
            }
            printf("%x:%s:%d:%d:%s\n", timestamp, players[player]->name, channel, length, message);
            free(message);
        }
    }
}

void parse_actions(struct info *mpq, struct player **players)
{
    return;
}

/*
 * Do some tests on the replay file passed in via argv[1]
 */
int main(int argc, char **argv)
{
    struct info mpq;
    struct info header;
    struct data_block data;
    struct player **players = NULL;
    struct map map;
    struct header hdr;
    char buf[1024];
    int fp, c, mode;

   while ((c = getopt (argc, argv, "rdflm")) != -1) {
         switch (c)
           {
           case 'r':
             mode = 1;
             break;
           case 'd':
             mode = 2;
             break;
           case 'f':
             mode = 3;
             break;
           case 'l':
            mode = 4;
            break;
            case 'm':
                mode = 5;
                break;
           case '?':
             if (isprint (optopt))
               fprintf (stderr, "Unknown option `-%c'.\n", optopt);
             else
               fprintf (stderr,
                        "Unknown option character `\\x%x'.\n",
                        optopt);
             return 1;
           default:
             fprintf(stderr, "What?\n");
           }
    }

    switch (mode) {
        case 1:
            //if (!load_mpq_info(&mpq, "replay.details", argv[optind])) {
            if (!load_mpq_info(&mpq, "replay.message.events", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }
            write(1, mpq.buf, mpq.size);
            break;
        case 2:
            fp = open(argv[optind], 'r');
            lseek(fp, 0x10, SEEK_SET);
            read(fp, buf, 1024);
            header.offset = 0;
            header.buf = buf;
            header.size = 1024;

            if (!load_mpq_info(&mpq, "replay.details", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }

            read_data(&header, &data);
            print_data(&data);
            read_data(&mpq, &data);
            print_data(&data);
            break;
        case 3:
            fp = open(argv[optind], 'r');
            lseek(fp, 0x10, SEEK_SET);
            read(fp, buf, 1024);
            header.offset = 0;
            header.buf = buf;
            header.size = 1024;

            if (!load_mpq_info(&mpq, "replay.details", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }

            read_data(&header, &data);
            load_header(&data, &hdr);
            read_data(&mpq, &data);
            load_map(&data, &map);
            load_players(&data, &players);
            printf("###\n");
            if (!load_mpq_info(&mpq, "replay.message.events", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }
            parse_messages(&mpq, players);
            break;
        case 4:
        {
           char *filename, *file;
           int total_files, i;
           off_t size;
           mpq_archive_s *archive;
           libmpq__archive_open(&archive, argv[optind], -1);
           libmpq__archive_files(archive, &total_files);
           printf("Number of files: %d\n", total_files);
           libmpq__file_number(archive, "replay.game.events", &total_files);
           printf("game events number: %d\n", total_files);
           libmpq__file_number(archive, "replay.message.events", &total_files);
           printf("message events number: %d\n", total_files);
           libmpq__file_number(archive, "(listfile)", &total_files);
           printf("listfile number: %d\n", total_files);
           libmpq__file_unpacked_size(archive, total_files, &size);
           file = malloc(size);
           libmpq__file_read(archive, total_files, file, size, NULL);
           write(1, file, size);
        }
            break;
        case 5:
            if (!load_mpq_info(&mpq, "replay.details", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }

            read_data(&mpq, &data);
            load_players(&data, &players);
            if (!load_mpq_info(&mpq, "replay.message.events", argv[optind])) {
                printf("Unable to load replay file\n");
                return 1;
            }
            parse_messages(&mpq, players);
            break;
    }            

    return 0;

}
