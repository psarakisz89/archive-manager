#include "archive-manager.hpp"
#include <boost/filesystem.hpp>
#include <sstream>
#include <fcntl.h>  //open()

//debug
#include <iostream>

///////////////////////////////////////////
// Public Class Functions
///////////////////////////////////////////
bool archiveManager::open_archive(const std::string &archive_path, bool read_only)
{
	bool success = true;
	std::string ro_message = read_only ? "RO" : "RW";
	std::cout<<std::endl<<"...Opening Archive: "<<archive_path<<" ("<<ro_message<<")..."<<std::endl;
	if (this->m_archive_is_open)
	{
		std::cerr<<"An archive is already open! "<<this->m_archive_path<<std::endl;
		return false;
	}

	this->read_arch = archive_read_new();
	archive_read_support_filter_all(this->read_arch);
	archive_read_support_format_all(this->read_arch);

	if (read_only)
	{
		//In read-only mode we expect that the archive already exists. If it doesn't return failure
		this->m_read_file_desc = open(archive_path.c_str(), O_RDONLY);
		if (archive_read_open_fd(this->read_arch, this->m_read_file_desc, 10240)) 
		{
			std::cerr<<archive_error_string(this->read_arch)<<std::endl;
			success = false;
		}
		else
		{
			std::cout<<"Archive: "<<archive_path<<" opened successfully (RO)..."<<std::endl;
			this->m_archive_path = archive_path;
			this->m_archive_is_open = true;
			this->m_readonly = true;
		}
	}
	else
	{
		//In this case the archive might or might not exist.
		this->m_read_file_desc = open(archive_path.c_str(), O_CREAT|O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		this->write_arch = archive_write_new();
  		archive_write_set_format_pax_restricted(this->write_arch);
		this->m_write_file_desc = open(archive_path.c_str(), O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		//First try to open for reading.
		if(archive_read_open_fd(this->read_arch, this->m_read_file_desc, 10240))
		{
			//If this fails, it means that the archive wasn't present in the drive. 
			//We will not fail, but we will not allow actions like extract 
			std::cerr<<"R: "<<archive_error_string(this->read_arch)<<std::endl;
			success = false;
		}
		//Check we opened the archive successfully 
		if (archive_write_open_fd(this->write_arch, this->m_write_file_desc)) 
		{
			std::cerr<<"W: "<<archive_error_string(this->write_arch)<<std::endl;
			success = false;
		}
		else
		{
			std::cout<<"Archive: "<<archive_path<<" opened successfully (RW)..."<<std::endl;
			this->m_archive_path = archive_path;
			this->m_archive_is_open = true;
			this->m_readonly = false;
			//We need to set the header pos at the end of the archive in order to find the correct offset to start writing
			this->seek_end_of_archive();
		}
	}
	return success;
}

bool archiveManager::add_folder(const std::string &source_dir)
{
	bool success = true;
	if(!this->m_archive_is_open || this->m_readonly)
	{
		std::cout<<"Archive not open or open on read-only mode..."<<std::endl;
		return false;
	}
	std::cout<<"Archiving directory: "<<source_dir <<" -> "<<this->m_archive_path<<std::endl<<std::endl;
	
	if (boost::filesystem::exists(source_dir) && boost::filesystem::is_directory(source_dir))
	{
		boost::filesystem::path root_folder_path = source_dir;
		root_folder_path.remove_trailing_separator();
		boost::filesystem::recursive_directory_iterator it(source_dir);
		boost::filesystem::recursive_directory_iterator end;
		while (it != end)
		{
			// Check if current entry is a directory and if it is do not add it to the archive.
			// If it isn't a directory, then it's a file. We want to add that to the archive...
			if (!boost::filesystem::is_directory(it->path()))
			{
				// The following lines will maintain folder hierarchy inside the archive:
				// In order to do that we need to find the relative file path of each file wrt the source_dir provided
				boost::filesystem::path file_path = it->path();
				boost::filesystem::path relative_file_path;
				while (!boost::filesystem::equivalent(file_path, root_folder_path))
				{
					relative_file_path = file_path.leaf() / relative_file_path;
					file_path = file_path.parent_path();
				}
				// example: source_dir: /path/to/source_dir/
				//										dir1
				//										├── dir2
				//										│   └── file2
				//										└── file1
				//	processing file1:
				//					file_path = /path/to/source_dir/dir1/file1
				//					relative_file_path = dir1/file1
				//	processing file2:
				//					file_path = /path/to/source_dir/dir1/dir2/file2
				//					relative_file_path = dir1/dir2/file2
				this->create_new_entry(it->path().string(), relative_file_path.string());
				this->write_entry_to_archive(it->path().string());
				archive_entry_free(entry);
			}

			boost::system::error_code ec;
			// Increment the iterator to point to next entry in recursive iteration
			it.increment(ec);
			if (ec) 
			{
				std::cerr << "Error While Accessing : " << it->path().string() << " :: " << ec.message() << '\n';
				success = false;
				break;
			}
		}
	}
	else
	{
		std::cerr<<"Directory: "<<source_dir<<" does not exist..."<<std::endl;
		success = false;
	}
	return success;
}

bool archiveManager::entry_exists(const std::string &entry_path)
{
	std::cout<<"Searching for: "<<entry_path<<std::endl;
	if (!this->m_archive_is_open)
	{
		std::cout<<"Archive not open or open on read-only mode..."<<std::endl;
		return false;
	}
	bool found = false;
	this->reload_read_archive();
	int ret = archive_read_next_header(this->read_arch, &this->entry);
	while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK)
	{
		std::string file = archive_entry_pathname(this->entry);
		if (file.compare(entry_path)==0)
		{
			std::cout<<"Found: "<<file<<std::endl;
			found = true;
			break;
		}
		ret = archive_read_next_header(this->read_arch, &this->entry);
	}
	return found;
}

bool archiveManager::add_entry(const std::vector<std::string> &file_names)
{
	std::cout<<std::endl<<"...Append to Archive..."<<std::endl;
	if (!this->m_archive_is_open || this->m_readonly)
	{
		std::cout<<"Archive not open or open on read-only mode..."<<std::endl;
		return false;
	}
	bool success = true;

	//Now let's do the actual appending of files in the end of the archive
	for (std::string fname : file_names) 
	{
		if (boost::filesystem::exists(fname) && boost::filesystem::is_regular_file(fname))
		{
			boost::filesystem::path file_path(fname);
			this->create_new_entry(file_path.string(), file_path.filename().string());
			this->write_entry_to_archive(fname);
			archive_entry_free(entry);
		}
		else
		{
			std::cout<<fname <<" is not a valid file... Skipping."<<std::endl;
		}
		
	}
	return success;	
}

bool archiveManager::extract_entries(std::string &target_dir, const std::vector<std::string> *file_names)
{
	std::cout<<std::endl<<"...Extract Files from Archive..."<<std::endl;
	bool success = true;
	int flags = ARCHIVE_EXTRACT_TIME;
	bool extract_all = file_names ? false : true;
	if (!this->m_archive_is_open)
	{
		std::cout<<"Archive not open..."<<std::endl;
		return false;
	}
	struct archive *disk;
	disk = archive_write_disk_new();
	if (archive_write_disk_set_options(disk, flags)) 
	{
		std::cerr<<archive_error_string(disk)<<std::endl;
		return false;
	}
	
	this->reload_read_archive();

	std::cout<<"Extracting: "<<this->m_archive_path<<" -> "<<target_dir<<std::endl;
	int ret = archive_read_next_header(this->read_arch, &this->entry);
	while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK)
	{
		std::string entry_source_path(archive_entry_pathname(this->entry));
		if (extract_all || std::find(file_names->begin(), file_names->end(), entry_source_path) != file_names->end())
		{
			// Element in vector.
			std::string entry_target_path = target_dir+entry_source_path;
			archive_entry_set_pathname(this->entry, entry_target_path.c_str());
			std::cout<<"Extracting File: "<<entry_source_path<<" -> "<<entry_target_path<<std::endl;
			this->write_entry_to_disk(disk);
		}
		else
		{
			std::cout<<"Skipping File: "<<entry_source_path<<std::endl;
		}
		
		ret = archive_read_next_header(this->read_arch, &this->entry);
	}
	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) 
	{
		std::cerr<<archive_error_string(this->read_arch)<<std::endl;
		success = false;
	}

	//Close and free the disk
	archive_write_close(disk);
  	archive_write_free(disk);
	
	return success;
}

std::vector<uint8_t> archiveManager::get_entry(const std::string &entry_path)
{
	std::cout<<"...Read Entry from Archive..."<<std::endl;
	bool success = true;
	const void *buff;
	size_t size;
	int64_t offset;
	if (!this->m_archive_is_open)
	{
		std::cout<<"Archive not open..."<<std::endl;
		return {};
	}
	

	//First let's try to find the entry. If it exists, the header will be at the correct spot in the archive.
	if(this->entry_exists(entry_path))
	{
		std::string entry_source_path(archive_entry_pathname(this->entry));		
		std::cout<<"entry SP: "<<entry_source_path<<std::endl;
		this->entry_exists(entry_path);

		const void *buff;
		size_t size;
		int64_t offset;
		int ret = archive_read_data_block(this->read_arch, &buff, &size, &offset);
		std::ostringstream temp;
		while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK) 
		{	
			std::cout<<"temp prev = "<<temp.str()<<std::endl;
			temp<<reinterpret_cast<char *>(const_cast<void *>(buff));
			std::cout<<"buff = "<<reinterpret_cast<char *>(const_cast<void *>(buff))<<" temp = "<<temp.str()<<" size = "<<size<<std::endl;
			ret = archive_read_data_block(this->read_arch, &buff, &size, &offset);
		}
		if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) 
		{
			std::cerr<<archive_error_string(this->read_arch)<<std::endl;
			return {};
		}

		std::istringstream temp2(temp.str());

		temp2.seekg(0, std::ios::end);
		auto data_size = temp2.tellg();
		std::cout<<data_size<<std::endl;
		temp2.seekg(0, std::ios::beg);
		std::vector<uint8_t> data(data_size);
		if (temp2.read(reinterpret_cast<char *>(data.data()), data_size))
		{
			std::cout<<"OK?"<<std::endl;
			return data;
		}
		else
		{
			return {};
		}
	}
	return {};
}


bool archiveManager::close_archive()
{
	bool success = true;
	if (this->m_archive_is_open)
	{
		std::cout<<"...Closing Archive..."<<std::endl;
		std::cout<<this->m_archive_path<<std::endl;
		
		if (this->m_readonly)
		{
			archive_read_close(this->read_arch);
			archive_read_free(this->read_arch);
			close(this->m_read_file_desc);
		}
		else
		{
			archive_write_close(this->write_arch);
  			archive_write_free(this->write_arch);
			close(this->m_write_file_desc);

		}
		this->m_archive_is_open = false;
		this->m_read_file_desc = -1;
		this->m_write_file_desc = -1;
		this->m_archive_path.clear();
		this->m_readonly = true;
	}
	else
	{
		std::cerr<<"No archive is open..."<<std::endl;
		success = false;
	}
	return success;	
}

///////////////////////////////////////////
// Private Class Functions
///////////////////////////////////////////


void archiveManager::create_new_entry(const std::string &disk_file_path, const std::string &archive_file_path)
{
	struct stat st;
	stat(disk_file_path.c_str(), &st);
	std::cout<<"Creating: "<<archive_file_path<<" entry..."<<std::endl;
	this->entry = archive_entry_new();
	archive_entry_set_pathname(this->entry, archive_file_path.c_str());
	archive_entry_set_size(this->entry, st.st_size); // Note 3
	archive_entry_set_filetype(this->entry, AE_IFREG);
	archive_entry_set_perm(this->entry, 0644);
}


bool archiveManager::write_entry_to_disk(archive *disk)
{
	bool success = true;
	const void *buff;
	size_t size;
	int64_t offset;

	int ret = archive_write_header(disk, this->entry);
	if (ret != ARCHIVE_OK) 
	{
		success = false;
		std::cerr<<archive_error_string(disk)<<std::endl;
	}
	else 
	{
		ret = archive_read_data_block(this->read_arch, &buff, &size, &offset);
		while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK) 
		{	
			ret = archive_write_data_block(disk, buff, size, offset);
			if (ret != ARCHIVE_OK) 
			{
				success = false;
				std::cerr<<archive_error_string(disk)<<std::endl;
				break;
			}
			ret = archive_read_data_block(this->read_arch, &buff, &size, &offset);
		}
		if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) 
		{
			success = false;
			std::cerr<<archive_error_string(this->read_arch)<<std::endl;
		}
	}
	return success;
}

bool archiveManager::write_entry_to_archive(const std::string &absolute_file_path)
{
	bool success = true;
	char buff[10240];
	int len;
	int fd;
	int ret = archive_write_header(this->write_arch, this->entry);
	std::cout<<"Writing :"<<absolute_file_path<<" -> "<< archive_entry_pathname(this->entry)<<std::endl;
	if (ret < ARCHIVE_OK) 
	{
		std::cerr<<archive_error_string(this->write_arch)<<std::endl;
		success = false;
	}
	if (ret > ARCHIVE_FAILED) 
	{
		fd = open(absolute_file_path.c_str(), O_RDONLY);
		len = read(fd, buff, sizeof(buff));
		
		while ( len > 0 ) 
		{
			archive_write_data(this->write_arch, buff, len);
			len = read(fd, buff, sizeof(buff));
		}
		close(fd);
	}
	return success;
}

void archiveManager::seek_end_of_archive()
{
	//get the file descriptor for the archive
	struct archive *temp_arch;
	struct archive_entry *temp_entry;

	int fd = open(this->m_archive_path.c_str(), O_RDONLY);
	temp_arch = archive_read_new();
	archive_read_support_filter_all(temp_arch);
	archive_read_support_format_all(temp_arch);
	// Open the archive for reading purposes. 
	// We need this to find where the archive ends so we can write the new files
	archive_read_open_fd(temp_arch, fd, 10240);
	while (archive_read_next_header(temp_arch, &temp_entry) == ARCHIVE_OK){}
	//save the offset that we will use to append files
	off_t offset = archive_read_header_position(temp_arch);
	//Now we can close the read archive
	archive_read_close(temp_arch);
	archive_read_free(temp_arch);
	close(fd);
	std::cout<<"offset = "<<offset<<std::endl;
	lseek(this->m_write_file_desc, offset, SEEK_SET);
}

void archiveManager::reload_read_archive()
{
	this->read_arch = archive_read_new();
	archive_read_support_filter_all(this->read_arch);
	archive_read_support_format_all(this->read_arch);
	//Make sure that the header is on the beginning of the archive
	lseek(this->m_read_file_desc, 0, SEEK_SET);
	if (archive_read_open_fd(this->read_arch, this->m_read_file_desc, 10240))
	{
		std::cerr<<archive_error_string(this->read_arch)<<std::endl;
	}
}

int main ()
{
	archiveManager arc;
	
	arc.open_archive("/home/psarakisz89/Downloads/temp.tar", false);
	// arc.add_folder("/home/psarakisz89/Documents/Repos/archive-manager/tempdir/");

	std::vector<std::string> to_extract, to_append;


	to_append.push_back("/home/psarakisz89/Documents/Repos/archive-manager/newfile.txt");
	to_append.push_back("/home/psarakisz89/Documents/Repos/archive-manager/tempdir/pics/2020-04-27-205917_5760x1080_scrot.png");
	arc.add_entry(to_append);
	to_append.clear();
	to_append.push_back("/home/psarakisz89/Documents/Repos/archive-manager/newfile");
	arc.add_entry(to_append);
	arc.close_archive();

	arc.open_archive("/home/psarakisz89/Downloads/temp.tar", true);
	std::vector<uint8_t> data = arc.get_entry("newfile.txt");
	arc.close_archive();


	// to_extract.push_back("pics/2020-04-27-212900_5760x1080_scrot.png");
	// to_extract.push_back("tempfile");
	// to_extract.push_back("tempfile2");
	// to_extract.push_back("tempfile10");
	// to_extract.push_back("newfile2");
	// std::string target_folder("/home/psarakisz89/Downloads/temp/");
	// std::string target_folder2("/home/psarakisz89/Downloads/temp2/");

	// to_extract.push_back("newfile.txt");
	// arc.extract_entries(target_folder2, &to_extract);
	// arc.close_archive();

	// arc.open_archive("/home/psarakisz89/Downloads/temp.tar", true);
	// arc.extract_entries(target_folder);
	// arc.extract_entries(target_folder2);
	// // arc.extract_entries(target_folder2, &to_extract);
	// arc.close_archive();

	// arc.open_archive("/home/psarakisz89/Downloads/temp.tar", false);
	// arc.entry_exists("newfile.txt");
	// if(!arc.entry_exists("newfile2"))
	// {
	// 	to_append.clear();
	// 	to_append.push_back("/home/psarakisz89/Documents/Repos/archive-manager/newfile2");
	// 	arc.add_entry(to_append);
	// }
	// arc.entry_exists("newfile2");
	// arc.entry_exists("pics/2020-04-27-205917_5760x1080_scrot.png");
	// arc.close_archive();



	// arc.extract_files_from_archive("/home/psarakisz89/Documents/Repos/code-test/compressed.tar", target_folder2);

	return 0;
}