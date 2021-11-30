#include "stdafx.h"
#include "fat_reader.h"

// Extract file from filesystem image
void test_extract_file(fat_context* ctx, fat_file* file, char* save_dir)
{
    char file_name[SHORT_NAME_LEN + 2];
    get_entry_name(file->entry, file_name);
    
    char file_path[64];
    strcpy(file_path, save_dir);
    strcat(file_path, "\\");
    strcat(file_path, file_name);
    
    FILE* of = fopen(file_path, "wb");
    
    char buf[BUF_SIZE];
    int n_read;
    do
    {
        n_read = file_read(ctx, file, (uint8_t*)buf, BUF_SIZE);
        fwrite(buf, 1, n_read, of);
    }
    while(n_read > 0);
    
    fclose(of);
}

// Perform filesystem read tests
void do_tests(fat_context* ctx, bool list_dir, bool read_file)
{
    if (list_dir)
    {
        // Directory contents listing
        char* test_dir_path = "\\INCLUDE";
        printf("\nDirectory path: %s\n", test_dir_path);
        file_entry* test_dir = get_entry(ctx, test_dir_path);
    
        if(test_dir != NULL)
            read_dir(ctx, test_dir);
        else
            puts("Directory not found!");

        free(test_dir);
    }
    
    if (read_file)
    {
        // File read test
        char* test_file_path = "\\INCLUDE\\WIFI.H";
        printf("\nFile path: %s\n", test_file_path);
        file_entry* test_file = get_entry(ctx, test_file_path);
    
        if(test_file != NULL)
        {
            fat_file* file = open_file(ctx, test_file);
            test_extract_file(ctx, file, "D:\\Temp");
            close_file(file);

            puts("File extracted");
        }
        else
            puts("File not found!");

        free(test_file);
    }
}

// Program entry point
int main(int argc, char** argv) {
    char* img_filename = "D:\\Temp\\fat16.img";
    
    puts("FAT filesystem image reader");
    
    FILE* fat_file = fopen(img_filename, "rb");
    
    if(fat_file != NULL && ferror(fat_file) == 0)
    {
        printf("Image file: %s\n\n", img_filename);
        
        fat_context ctx;
        bool can_read = parse_fat_image(&ctx, fat_file);
        
        if (can_read)
            do_tests(&ctx, true, true);
        else
            puts("Error parsing FAT image!");

        fclose(fat_file);
    }
    else
        printf("File %s open error!\n", img_filename);

    _CRT_UNUSED(getchar());
    
    return 0;
}
