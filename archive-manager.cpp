#include "archive-manager.hpp"
#include <boost/filesystem.hpp>
#include <fcntl.h>  //open()

//debug
#include <iostream>

///////////////////////////////////////////
// Public Class Functions
///////////////////////////////////////////

bool archiveManager::create_archive(const std::string &archive_name, const std::string &source_dir)
{
	bool success = true;
	std::cout<<std::endl<<"...Create Archive..."<<std::endl;
	std::cout<<"Archiving directory: "<<source_dir <<" -> "<<archive_name<<std::endl<<std::endl;
	//First open the file we want to write the archive into
	this->arch = archive_write_new();
  	archive_write_set_format_pax_restricted(this->arch);
	archive_write_open_filename(this->arch, archive_name.c_str());
	
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
	archive_write_close(this->arch);
  	archive_write_free(this->arch);
	return success;
}

bool archiveManager::add_files_to_archive(const std::string &archive_name, const std::vector<std::string> &file_names)
{
	std::cout<<std::endl<<"...Append to Archive..."<<std::endl;
	bool success = true;

	int fd = open(archive_name.c_str(), O_WRONLY);
	//We need to seek the end of the archive in order to find the correct offset to start writing
	off_t offset = this->get_offset(archive_name);
	//Now set the offset of the filedescriptor
	lseek(fd, offset, SEEK_SET);
	
	//Let's open the archive for writing...
	this->arch = archive_write_new();
	archive_write_set_format_pax_restricted(this->arch);
	//and connect it with the file descriptor
	int ret = archive_write_open_fd(this->arch, fd);
	if (ret) 
	{
		std::cerr<<archive_error_string(this->arch)<<std::endl;
		success = false;
	}
	if (success)
	{
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
	}
	//All done! Close and free the archive
	archive_write_close(this->arch);
	archive_write_free(this->arch);
	//Close the file in the disk we just wrote
	close(fd);
	return success;	
}

bool archiveManager::extract_files_from_archive(const std::string &archive_name, std::string &target_dir, const std::vector<std::string> *file_names)
{
	std::cout<<std::endl<<"...Extract Specific Files from Archive..."<<std::endl;
	bool success = true;
	int flags = ARCHIVE_EXTRACT_TIME;
	bool extract_all = file_names ? false : true;

	this->arch = archive_read_new();
	this->disk = archive_write_disk_new();
	archive_read_support_filter_all(this->arch);
	archive_read_support_format_tar(this->arch);
	archive_write_disk_set_options(this->disk, flags);

	std::cout<<"Extracting: "<<archive_name<<" -> "<<target_dir<<std::endl;
	const char* arch_name = archive_name.c_str();

	//Try to open the archive
	int ret = archive_read_open_filename(this->arch, arch_name, 10240);
	//Check we opened the archive successfully 
	if (ret) 
	{
		std::cerr<<archive_error_string(this->arch)<<std::endl;
		success = false;
	}
	else
	{
		//OK the archive is open... Let's try reading the next header
		ret = archive_read_next_header(this->arch, &this->entry);
		while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK)
		{
			std::string entry_source_path(archive_entry_pathname(this->entry));
			if (extract_all || std::find(file_names->begin(), file_names->end(), entry_source_path) != file_names->end())
			{
  				// Element in vector.
				std::string entry_target_path = target_dir+entry_source_path;
				archive_entry_set_pathname(this->entry, entry_target_path.c_str());
				std::cout<<"Extracting File: "<<entry_source_path<<" -> "<<entry_target_path<<std::endl;
				this->write_entry_to_disk();
			}
			else
			{
				std::cout<<"Skipping File: "<<entry_source_path<<std::endl;
			}
			
			ret = archive_read_next_header(this->arch, &this->entry);
		}
		if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) 
		{
			std::cerr<<archive_error_string(this->arch)<<std::endl;
			success = false;
		}

	}
	//Close and free the archive
	archive_read_close(this->arch);
	archive_read_free(this->arch);
	//Close and free the disk
	archive_write_close(this->disk);
  	archive_write_free(this->disk);
	return success;

}

///////////////////////////////////////////
// Private Class Functions
///////////////////////////////////////////


void archiveManager::create_new_entry(const std::string &disk_file_path, const std::string &archive_file_path)
{
	stat(disk_file_path.c_str(), &this->st);
	std::cout<<"Creating: "<<archive_file_path<<" entry..."<<std::endl;
	this->entry = archive_entry_new();
	archive_entry_set_pathname(this->entry, archive_file_path.c_str());
	archive_entry_set_size(this->entry, this->st.st_size); // Note 3
	archive_entry_set_filetype(this->entry, AE_IFREG);
	archive_entry_set_perm(this->entry, 0644);
}


bool archiveManager::write_entry_to_disk()
{
	bool success = true;
	const void *buff;
	size_t size;
	int64_t offset;

	int ret = archive_write_header(this->disk, this->entry);
	if (ret != ARCHIVE_OK) 
	{
		success = false;
		std::cerr<<archive_error_string(this->arch)<<std::endl;
	}
	else 
	{
		ret = archive_read_data_block(this->arch, &buff, &size, &offset);
		while (ret != ARCHIVE_EOF && ret == ARCHIVE_OK) 
		{	
			ret = archive_write_data_block(this->disk, buff, size, offset);
			if (ret != ARCHIVE_OK) 
			{
				success = false;
				std::cerr<<archive_error_string(this->arch)<<std::endl;
				break;
			}
			ret = archive_read_data_block(this->arch, &buff, &size, &offset);
		}
		if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) 
		{
			success = false;
			std::cerr<<archive_error_string(this->arch)<<std::endl;
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
	int ret = archive_write_header(this->arch, this->entry);
	std::cout<<"Writing :"<<absolute_file_path<<" -> "<< archive_entry_pathname(this->entry)<<std::endl;
	if (ret < ARCHIVE_OK) 
	{
		std::cerr<<archive_error_string(this->arch)<<std::endl;
		success = false;
	}
	if (ret > ARCHIVE_FAILED) 
	{
		fd = open(absolute_file_path.c_str(), O_RDONLY);
		len = read(fd, buff, sizeof(buff));
		
		while ( len > 0 ) 
		{
			archive_write_data(this->arch, buff, len);
			len = read(fd, buff, sizeof(buff));
		}
		close(fd);
	}
	return success;
}

off_t archiveManager::get_offset(const std::string &archive_name)
{
	//get the file descriptor for the archive
	int fd = open(archive_name.c_str(), O_RDWR, 0666);
	this->arch = archive_read_new();
	archive_read_support_filter_all(this->arch);
	archive_read_support_format_tar(this->arch);
	// Open the archive for reading purposes. 
	// We need this to find where the archive ends so we can write the new files
	archive_read_open_fd(this->arch, fd, 10240);
	while (archive_read_next_header(this->arch, &this->entry) == ARCHIVE_OK){}
	//save the offset that we will use to append files
	off_t offset = archive_read_header_position(this->arch);
	//Now we can close the read archive
	archive_read_close(this->arch);
	archive_read_free(this->arch);
	return offset;
}

int main ()
{
	archiveManager arc;
	arc.create_archive("/home/psarakisz89/Documents/Repos/code-test/compressed.tar", "/home/psarakisz89/Documents/Repos/code-test/tempdir");	
	std::vector<std::string> to_extract, to_append;
	to_extract.push_back("pics/2020-04-27-212900_5760x1080_scrot.png");
	to_extract.push_back("tempfile");
	to_extract.push_back("tempfile2");
	to_extract.push_back("tempfile10");
	to_extract.push_back("newfile2");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/pics/2020-04-27-205917_5760x1080_scrot.png");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/pics/2020-04-27-211244_5760x1080_scrot.png");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/pics/2020-04-27-212900_5760x1080_scrot.png");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/newfile");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/newfile2");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/newfile3");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/tempdir2/newfile4");
	to_append.push_back("/home/psarakisz89/Documents/Repos/code-test/newtempfile");
	// arc.add_files_to_archive("/home/psarakisz89/Documents/Repos/code-test/compressed.tar", to_append);
	
	std::string target_folder("/home/psarakisz89/Downloads/temp/");
	std::string target_folder2("/home/psarakisz89/Downloads/temp2/");
	arc.extract_files_from_archive("/home/psarakisz89/Documents/Repos/code-test/compressed.tar", target_folder, &to_extract);
	arc.extract_files_from_archive("/home/psarakisz89/Documents/Repos/code-test/compressed.tar", target_folder2);

	return 0;
}