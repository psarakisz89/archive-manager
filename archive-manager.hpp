#pragma once
#include <string>
#include <vector>
#include <archive.h>
#include <archive_entry.h>
/**
 * @brief This class is responsible for communicating with the libarchive library. It provides functionality to:
 * 1. Create an archive from a target directory
 * 2. Check if an entry exists to an archive
 * 3. Add files to an already existing archive
 * 4. Extract all or specific files from an already existing archive
 */
class archiveManager
{
public:
	archiveManager() = default;
	~archiveManager() = default;
	
	/**
	 * @brief Open an archive from the disk
	 * 
	 * @param archive_path The absolute path of an already existing archive
	 * @param read_only whether the archive will be opened with writting priviliges or not
	 * @return true if openning the archive was successful
	 * @return false otherwise
	 */
	bool open_archive(const std::string &archive_path, bool read_only);

	/**
	 * @brief create entries to an open archive from the contents of a directory. Requires opening an archive first
	 * 
	 * @param source_dir The path of the directory containing the files/folders we wish to archive. Absolute path is required
	 * @return true If the archive was created successfully 
	 * @return false otherwise
	 */
	bool add_folder(const std::string &source_dir);

	/**
	 * @brief Checks if the provided entry exists in an archive. Requires opening an archive first
	 * 
	 * @param entry_path The path of the entry inside the archive
	 * @return true if the entry was found in the archive
	 * @return false otherwise
	 */
	bool entry_exists(const std::string &entry_path);

	/**
	 * @brief Add a list of files to an already existing archive. Requires opening an archive first
	 * 
	 * @param file_names a vector of file names. The absolute path of each file is required
	 * @return true if the files are added successfully to the archive
	 * @return false otherwise
	 */
	bool add_entry(const std::vector<std::string> &file_names);
	
	/**
	 * @brief Extracts all the files that are contained in an archive. Optionally, if a list of specific files is provided, only specific files will be extracted.
	 * 	Requires opening an archive first in read-only mode.
	 * 
	 * @param target_dir The absolute path of the target directory to extract the files
	 * @param file_names (Optional argument). If a list of file names is provided, only those files will be extracted from the archive
	 * @return true if the files are extracted successfully
	 * @return false otherwise
	 */
	bool extract_entries(std::string &target_dir, const std::vector<std::string>* entries_path = nullptr);

	/**
	 * @brief Returns the data contained in an archive entry
	 * 
	 * @param entry_path path of the entry in the archive
	 * @return std::vector<uint8_t> vector of bytes that represent the data in the entry
	 */
	std::vector<uint8_t> get_entry(const std::string &entry_path);

	/**
	 * @brief Close the opened archive
	 * 
	 * @return true if closed successfully
	 * @return false otherwise
	 */
	bool close_archive();
private:
	struct archive *read_arch;
	struct archive *write_arch;
	struct archive_entry *entry;

	int m_read_file_desc;
	int m_write_file_desc;
	bool m_archive_is_open{false};
	bool m_readonly;
	std::string m_archive_path;
	/**
	 * @brief Defines and creates a new entry which can be later added to an archive
	 * 
	 * @param disk_file_path the absolute path of the file in the disk
	 * @param archive_file_path the path of the file in the archive. Notice that these 2 paths will not be the same and they can be distinguished in 2 cases:
	 * Case1: add_folder(): 
	 * 			disk_file_path is the absolute path of each file in the source_dir
	 * 			archive_file_path maintains the file/folder hierarchy in the archive. See the explanation in the implementation of create_archive().
	 * Case2: add_entry():
	 * 			disk_file_path is the absolute path of each file provided in the file_names vector
	 * 			archive_file_path will always be the root folder of the already existing archive. i.e. file/folder hierarchy will not be maintained in this case 
	 */
	void create_new_entry(const std::string &disk_file_path, const std::string &archive_file_path);

	/**
	 * @brief Writes whatever the struct write_arch is pointing to, in whatever the struct disk is pointing to. 
	 * This functions is called only when extracting an archive to the disk. 
	 * 
	 * @return true if the archive entry was written to disk successfully
	 * @return false otherwise
	 */
    bool write_entry_to_disk(archive *disk);

	/**
	 * @brief Writes the data in absolute_file_path in the archive targeted by write_arch. The archive entry is defined from the entry struct
	 * 
	 * @param absolute_file_path the absolute path of the file we want to add to the archive
	 * @return true if the file was added to the archive
	 * @return false otherwise
	 */
	bool write_entry_to_archive(const std::string &absolute_file_path);

	/**
	 * @brief In order to append to an existing archive, we need to find the end of the data that is already in the archive and start writing from there.
	 * 
	 * @return off_t the offset pointing at the end of the data of the archive
	 */
	void seek_end_of_archive();

	/**
	 * @brief //Everytime we perform read related actions (like extract_entries() or entry_exists()), 
	 * we need to make sure that we start parsing the archive from the beginning. The following lines take care of that
	 * 
	 */
	void reload_read_archive();
};