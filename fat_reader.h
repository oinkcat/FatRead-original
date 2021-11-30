#pragma once
#include "stdafx.h"

#define OEM_NAME_LEN 8
#define LABEL_LEN 11
#define FS_TYPE_LEN 8
#define SHORT_NAME_LEN 11
#define NAME_PART_LEN 8
#define EXT_PART_LEN 3

#define EMPTY_ENTRY 0xE5

// Entry attributes
#define ATTR_READONLY		0x01
#define ATTR_HIDDEN			0x02
#define ATTR_SYSTEM			0x04
#define ATTR_VOLUMEID		0x08
#define ATTR_DIRECTORY		0x10
#define ATTR_ARCHIVE		0x20

#define BUF_SIZE (8 * 1024)

#define DIR_ENTRY_SIZE 32

// FAT fs type
enum class fat_type : int
{
	FAT16, FAT32
};

#pragma pack(1)

// Common FAT header
struct fat_header
{
	char jmp_code[3];
	char oem_name[OEM_NAME_LEN];
	uint16_t bytes_per_sect;
	uint8_t sect_per_clust;
	uint16_t rsvd_count;
	uint8_t num_fats;
	uint16_t num_root_ent;
	uint16_t tot_sect_16;
	uint8_t media_type;
	uint16_t fat_size_16;
	uint16_t sect_per_track;
	uint16_t num_heads;
	uint32_t num_hidden_sect;
	uint32_t tot_sect_32;
};

// FAT16 info
struct fat16_info
{
	uint8_t drv_num;
	uint8_t rsvd1;
	uint8_t boot_sig;
	uint32_t volume_id;
	char volume_label[LABEL_LEN];
	char fs_type[FS_TYPE_LEN];
};

// FAT32 info
struct fat32_info
{
	uint32_t fat_size_32;
	uint16_t ext_flags;
	uint16_t fs_version;
	uint32_t root_cluster;
	uint16_t fsinfo_sector;
	uint16_t bkp_sector;
	char reserved[12];
	struct fat16_info inherited;
};

// FAT file entry
struct file_entry
{
	char short_name[SHORT_NAME_LEN];
	uint8_t attrs;
	uint8_t rsvd;
	uint8_t crt_time_ms;
	uint16_t create_time;
	uint16_t create_date;
	uint16_t access_date;
	uint16_t cluster_h;
	uint16_t write_time;
	uint16_t write_date;
	uint16_t cluster_l;
	uint32_t size;
};

// Filesystem context
struct fat_context
{
	FILE* file;
	enum fat_type type;
	uint32_t root_offset;
	uint32_t cluster_bytes;
	uint16_t max_entries;
	union
	{
		uint16_t* table16;
		uint32_t* table32;
	};
};

// Type aliases
typedef struct fat_header fat_header;
typedef struct fat16_info fat16_info;
typedef struct fat32_info fat32_info;
typedef struct file_entry file_entry;
typedef struct fat_context fat_context;
typedef struct dir_list_info dir_list_info;
typedef struct fat_file fat_file;

// Entry processing function
typedef bool(*entry_proc)(file_entry*, void*);

// Directory contents listing info
struct dir_list_info
{
	uint32_t cluster;
	entry_proc proc;
	void* proc_arg;
};

// Open file context
struct fat_file
{
	file_entry* entry;
	uint32_t pos;
	uint32_t current_cluster;
};

#pragma pack(pop)

// Function declarations
extern bool parse_fat_image(fat_context* context, FILE* file);
extern void get_entry_name(file_entry* entry, char* namebuf);
extern file_entry* get_entry(fat_context* ctx, char* name);
extern fat_file* open_file(fat_context* ctx, file_entry* entry);
extern int file_read(fat_context* ctx, fat_file* file, uint8_t* data, uint32_t length);
extern void close_file(fat_file* file);
extern void read_dir(fat_context* ctx, file_entry* directory);
