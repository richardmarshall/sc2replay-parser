#ifndef __PARSE_H__
#define __PARSE_H__

/* Block types */
#define TYPE_BIN 0x02
#define TYPE_ARRAY 0x04
#define TYPE_DICT 0x05
#define TYPE_BYTE 0x06
#define TYPE_INT 0x07
#define TYPE_SDNV 0x09

struct player {
    char *name;
    char *race;
    int color[4];
    int team;
};

struct header {
    int major;
    int minor;
    int rev;
    int build;
    int durration;
};

struct map {
    char *name;
    char *minimap;
};

struct info {
    mpq_archive_s *archive;
    off_t size;
    char *buf;
    int offset;
    char cur;
    char prev;
};

struct data_block {
    int size;
    char type;
    void *data;
};

struct data_dict {
    long *keys;
    struct data_block **values;
};

char next(struct info *mpq);
char peek(struct info *mpq);
short peek_short(struct info *mpq);
int next_int(struct info *mpq);
short next_short(struct info *mpq);
int64_t decode_sdnv(struct info *mpq);
void print_depth(int depth);

void read_data(struct info *mpq, struct data_block *data);
void print_binary(int size, char *data);
void print_data(struct data_block *data);
int load_mpq_info(struct info *mpq, char *ifile, char *file);
struct data_block *get_dict_index(struct data_block *dict, int index);
struct data_block *get_array_index(struct data_block *arr, int index);

void load_map(struct data_block *data, struct map *map);
void load_header(struct data_block *data, struct header *hdr);
void load_players(struct data_block *data, struct player ***players);

#endif

