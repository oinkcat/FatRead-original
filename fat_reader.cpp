#include "stdafx.h"
#include "fat_reader.h"
#include <assert.h>

// Print FS information
static void dump_headers_info(fat_header* common, void* fat_info, bool is32Bit)
{
	puts("FAT common header info:");
	// Dump common info
	printf("Formatted OS name: %s\n", common->oem_name);
	printf("Bytes per sector: %u\n", common->bytes_per_sect);
	printf("Sectors per cluster: %u\n", common->sect_per_clust);
	printf("Reserved sectors count: %u\n", common->rsvd_count);
	printf("Number of FATs: %u\n", common->num_fats);
	printf("Root directory entries count: %u\n", common->num_root_ent);
	printf("Media code: %X\n", common->media_type);

	if (common->tot_sect_16 != 0)
		printf("Total sectors count (small): %u\n", common->tot_sect_16);
	else
		printf("Total sectors count (large): %u\n", common->tot_sect_32);

	printf("FAT size of sectors: %u\n\n", common->fat_size_16);

	puts(is32Bit ? "FAT type: FAT32" : "FAT type: FAT16");

	if (!is32Bit)
	{
		puts("FAT16 info:");
		fat16_info* fat16 = (fat16_info*)fat_info;
		printf("Volume serial number: %X\n", fat16->volume_id);

		char label[12];
		strncpy(label, fat16->volume_label, 11);
		label[11] = '\0';
		printf("Volume label: %s\n", label);
	}
}

// Read file and fill filesystem context
bool parse_fat_image(fat_context* context, FILE* file)
{
	// Read headers
	fat_header common_hdr;
	fread(&common_hdr, 1, sizeof(fat_header), file);

	fat16_info fat16_hdr;
	fat32_info fat32_hdr;

	// Determine FAT type (!)
	bool is32Bit = common_hdr.fat_size_16 == 0;
	if (is32Bit)
		fread(&fat32_hdr, 1, sizeof(fat32_info), file);
	else
		fread(&fat16_hdr, 1, sizeof(fat16_info), file);

	if (!is32Bit && strncmp(fat16_hdr.fs_type, "FAT12", 5) == 0)
	{
		return false;
	}

	void* param = is32Bit ? (void*)&fat32_hdr : (void*)&fat16_hdr;
	dump_headers_info(&common_hdr, param, is32Bit);

	// Read FAT table
	uint32_t fat_start = common_hdr.rsvd_count * common_hdr.bytes_per_sect;
	fseek(file, fat_start, SEEK_SET);

	uint32_t fat_size;
	uint16_t* fat_table16;
	uint32_t* fat_table32;
	uint8_t media_type;

	// (!)
	if (is32Bit)
	{
		fat_size = fat32_hdr.fat_size_32 * common_hdr.bytes_per_sect;
		fat_table32 = new uint32_t[fat_size];
		fread(fat_table32, 1, fat_size, file);
		media_type = fat_table32[0] & 0xFF;
	}
	else
	{
		fat_size = common_hdr.fat_size_16 * common_hdr.bytes_per_sect;
		fat_table16 = new uint16_t[fat_size];
		fread(fat_table16, 1, fat_size, file);
		media_type = fat_table16[0] & 0xFF;
	}

	if (media_type != common_hdr.media_type)
	{
		puts("Invalid media size in FAT! Exiting");
		return false;
	}

	// Fill context data
	context->file = file;
	context->type = is32Bit ? fat_type::FAT32 : fat_type::FAT16;
	context->cluster_bytes = common_hdr.bytes_per_sect * common_hdr.sect_per_clust;
	context->root_offset = common_hdr.bytes_per_sect * common_hdr.rsvd_count +
						   fat_size * common_hdr.num_fats;
	context->max_entries = common_hdr.num_root_ent;

	// Pointer to FAT table
	if (is32Bit)
		context->table32 = fat_table32;
	else
		context->table16 = fat_table16;

	return true;
}

// Is entry is free
static inline bool is_free(file_entry* entry)
{
	return entry->short_name[0] == EMPTY_ENTRY;
}

// Is entry a Long File Name entry
static inline bool is_lfn(file_entry* entry)
{
	const int atr_lfn = ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUMEID;
	return entry->attrs & atr_lfn;
}

// Get correct entry name
void get_entry_name(file_entry* entry, char* namebuf)
{
	int sp_pos = strcspn(entry->short_name, " ");
	int name_len = sp_pos < NAME_PART_LEN ? sp_pos : NAME_PART_LEN;
	strncpy(namebuf, entry->short_name, name_len);
	if (entry->short_name[NAME_PART_LEN] != ' ')
	{
		namebuf[name_len] = '.';
		char* file_ext = entry->short_name + NAME_PART_LEN;
		int ext_len = strcspn(file_ext, " ");
		strncpy(namebuf + name_len + 1, file_ext, ext_len);
		name_len += ext_len + 1;
	}
	namebuf[name_len] = '\0';
}

// Jump file pointer at beginning of given cluster
static void seek_cluster(fat_context* ctx, uint32_t cluster, uint16_t offset)
{
	uint32_t cluster_pos;
	if (cluster > 1)
	{
		uint32_t data_offset = ctx->cluster_bytes * (cluster - 2);
		uint16_t root_size = ctx->max_entries * DIR_ENTRY_SIZE;
		cluster_pos = ctx->root_offset + root_size + data_offset;
	}
	else
	{
		cluster_pos = ctx->root_offset;
	}

	uint32_t new_pos = cluster_pos + offset;

	fseek(ctx->file, new_pos, SEEK_SET);
}

// Perform listing of directory with processing of arbitrary function
static void list_dir(fat_context* ctx, dir_list_info* list_info, file_entry* last)
{
	seek_cluster(ctx, list_info->cluster, 0);

	file_entry entry;
	bool reading = true;
	bool reached_end = false;
	uint32_t bytes_read = 0;

	do
	{
		int read_entry_size = fread(&entry, 1, sizeof(file_entry), ctx->file);
		assert(read_entry_size == DIR_ENTRY_SIZE);

		bytes_read += read_entry_size;

		if (entry.short_name[0] != 0)
		{
			reading = list_info->proc(&entry, list_info->proc_arg);

			// Check if directory cluster ended
			bool move_next;
			if (ctx->type == fat_type::FAT32 || list_info->cluster > 1)
			{
				move_next = bytes_read >= ctx->cluster_bytes;
			}
			else
			{
				// All entries in root directory must be read
				uint32_t n_entries_read = bytes_read / DIR_ENTRY_SIZE;
				move_next = n_entries_read >= ctx->max_entries;
			}

			if (reading && move_next)
			{
				// (!)
				uint32_t next_cluster = (ctx->type == fat_type::FAT32)
					? ctx->table32[list_info->cluster] & 0x0FFFFFFF
					: ctx->table16[list_info->cluster];

				seek_cluster(ctx, next_cluster, 0);
				list_info->cluster = next_cluster;
				bytes_read = 0;
			}
		}
		else
		{
			reading = false;
			reached_end = true;
		}
	} while (reading);

	if (last != NULL)
	{
		memcpy(last, &entry, sizeof(file_entry));
	}
}

// Display every entry info
static bool print_entry(file_entry* entry, void* arg)
{
	if (!is_free(entry) && !is_lfn(entry))
	{
		char entry_name[SHORT_NAME_LEN + 2];
		get_entry_name(entry, entry_name);
		puts(entry_name);
	}

	return true;
}

// Read directory at given cluster
void read_dir(fat_context* ctx, file_entry* directory)
{
	puts("Directory listings:");

	uint16_t cluster = directory->cluster_l;
	dir_list_info dir_display = { cluster, print_entry, NULL };
	list_dir(ctx, &dir_display, NULL);
}

// Check if entry name matches given
static bool match_entry(file_entry* entry, void* arg)
{
	if (!is_free(entry) && !is_lfn(entry))
	{
		char entry_name[SHORT_NAME_LEN + 2];
		get_entry_name(entry, entry_name);
		char* name_to_match = (char*)arg;
		bool matches = strcmp(entry_name, name_to_match) == 0;

		return !matches;
	}

	return true;
}

// Get directory cluster by name
file_entry* get_entry(fat_context* ctx, char* name)
{
	const char* delim = "\\";

	char* path = strdup(name);
	char* dir_part = strtok(path, delim);

	// Root directory
	file_entry* found_entry = new file_entry;
	found_entry->cluster_l = 1;

	while (dir_part != NULL)
	{
		uint16_t cluster = found_entry->cluster_l;
		dir_list_info dir_match = { cluster, match_entry, dir_part };
		list_dir(ctx, &dir_match, found_entry);

		if (found_entry->cluster_l == 0)
			return NULL;

		dir_part = strtok(NULL, delim);
		if (dir_part == 0)
			return found_entry;
	}

	// Root directory was requested
	strcpy(found_entry->short_name, "\\");

	return found_entry;
}

// Open file entry
fat_file* open_file(fat_context* ctx, file_entry* entry)
{
	fat_file* file = new fat_file;
	file->entry = entry;
	file->pos = 0;

	uint16_t ch = file->entry->cluster_h;
	uint16_t cl = file->entry->cluster_l;
	file->current_cluster = ch << 16 | cl;

	return file;
}

// Read file data
int file_read(fat_context* ctx, fat_file* file, uint8_t* data, uint32_t length)
{
	uint32_t bytes_read = 0;
	uint16_t offset = file->pos % ctx->cluster_bytes;
	seek_cluster(ctx, file->current_cluster, offset);

	while (bytes_read < length)
	{
		// Handle end of file
		uint32_t file_left = file->entry->size - file->pos;
		if (file_left <= 0)
			return bytes_read;

		// Amount of bytes to read from current cluster
		uint32_t cluster_left = ctx->cluster_bytes - offset;
		uint32_t read_count = length < cluster_left ? length : cluster_left;
		if (file_left < read_count)
			read_count = file_left;

		fread(data + bytes_read, 1, read_count, ctx->file);
		bytes_read += read_count;
		file->pos += read_count;
		offset = 0;

		// Get next file cluster if current ended
		if (cluster_left - read_count <= 0)
		{
			uint32_t new_cluster = (ctx->type == fat_type::FAT32)
				? ctx->table32[file->current_cluster] & 0x0FFFFFFF
				: ctx->table16[file->current_cluster];
			seek_cluster(ctx, new_cluster, 0);
			file->current_cluster = new_cluster;
		}
	}

	return bytes_read;
}

// Deallocate file info data
void close_file(fat_file* file)
{
	free(file);
}