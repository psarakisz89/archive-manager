#pragma once
#include <string>
#include <vector>
#include <archive.h>
#include <archive_entry.h>

class archiveManager
{
public:
	archiveManager() = default;
	~archiveManager() = default;

	bool create_archive(const std::string &archive_name, const std::string &source_dir);
	bool add_files_to_archive(const std::string &archive_name, const std::vector<std::string> &file_names);
	bool extract_files_from_archive(const std::string &archive_name, std::string &target_dir, const std::vector<std::string> *file_names = nullptr);
private:
	struct archive *arch;
    struct archive *disk;
	struct archive_entry *entry;
	struct stat st;	

	void create_new_entry(const std::string &disk_file_path, const std::string &archive_file_path);
	bool write_entry_to_disk();
	bool write_entry_to_archive(const std::string &absolute_file_path);

	//debugging
	off_t get_offset(const std::string &archive_name);
};
