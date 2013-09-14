/* ========================================================================= */
/* ------------------------------------------------------------------------- */
/*!
  \file			dir_iterator.c
  \date			September 2013
  \author		TNick
  
*//*


 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 Please read COPYING and README files in root folder
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
/* ------------------------------------------------------------------------- */
/* ========================================================================= */
//
//
//
//
/*  INCLUDES    ------------------------------------------------------------ */

#include "dir_iterator.h"

#include <stdlib.h>
#include <string.h>
#include <aitown/error_codes.h>
#include <aitown/dbg_assert.h>
#include <aitown/pointer_aritmetic.h>
#include <aitown/char_buff.h>

#ifdef AITOWN_WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	pragma warning (disable : 4996)
#else
#	include <dirent.h>
#	include <sys/stat.h>
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	include <regex.h>
#	include <errno.h>
#	include <err.h>
#endif

#ifdef AITOWN_WIN32
#	define strncasecmp _strnicmp
#endif

/*  INCLUDES    ============================================================ */
//
//
//
//
/*  DEFINITIONS    --------------------------------------------------------- */

typedef struct {
#ifdef AITOWN_WIN32
#else
	regex_t rex;
#endif
	const char *init_path;
	size_t init_path_sz;
	const char *name_filter;
	dir_iterator_flags_t flags;
	dir_iterator_foreach_t kb;
	void *user_data;
	
	char_buff_t chb;
	
	char path_separator;
} find_struct_t;

/*  DEFINITIONS    ========================================================= */
//
//
//
//
/*  DATA    ---------------------------------------------------------------- */

/*  DATA    ================================================================ */
//
//
//
//
/*  FUNCTIONS    ----------------------------------------------------------- */
/*
static int append_to_buffer ( find_struct_t* fs_, const char * p) {
	size_t len = strlen (p);
	char * tmp_buf;
	size_t new_used = fs_->chb.used + len;
	
	// enlarge the buffer if required
	if ( new_used+8 >= fs_->allocated_buffer ) {
		size_t new_alloc = new_used + 128 + ROUND_TO_PTR(new_used);
		tmp_buf = (char*)realloc(fs_->chb.data, new_alloc);
		if ( tmp_buf == NULL )
			return FUNC_MEMORY_ERROR;
		fs_->chb.data = tmp_buf;
		fs_->allocated_buffer = new_alloc;
	}
	
	// append the string
	char * dest_buf = fs_->chb.data + fs_->chb.used;
	memcpy (dest_buf, p, len);
	dest_buf[len] = 0;
	fs_->chb.used = new_used;
	
	return FUNC_OK;
}*/

#ifdef AITOWN_WIN32

static inline int is_entry_directory (WIN32_FIND_DATA * fd) {
	for (;;) {
		if ( (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			break;
		if ( fd->cFileName[0] == '.' ) {
			if ( fd->cFileName[1] == 0 ) {
				break;
			} else if ( fd->cFileName[1] == '.' ) {
				if ( fd->cFileName[2] == 0 )
					break;
			}
		}
		return 1;
	}
	return 0;
}

static inline int is_entry_file (WIN32_FIND_DATA * fd) {
	for (;;) {
		if ( (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
			break;
		if ( (fd->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) )
			break;
#ifdef _DEBUG
		if ( fd->cFileName[0] == '.' ) {
			if ( fd->cFileName[1] == 0 ) {
				DBG_ASSERT(0);
				break;
			} else if ( fd->cFileName[1] == '.' ) {
				if ( fd->cFileName[2] == 0 ) {
					DBG_ASSERT(0);
					break;
				}
			}
		}
#endif
		return 1;
	}
	return 0;
}

func_error_t dir_iterator_win (find_struct_t* fs_)
{
	int err_code = FUNC_OK;
	HANDLE h_find;
	WIN32_FIND_DATA find_data;

	// we want to return the buffer in same state as we've got it
	size_t used_data = fs_->chb.used;
	{ // don't use end_of_dir below, the buffer may be rellocated
		char *end_of_dir = fs_->chb.data + used_data;
		*end_of_dir = fs_->path_separator; end_of_dir++; fs_->chb.used++;
		*end_of_dir = 0;
	}
	
	// first, dig the childs
	if ( (fs_->flags & DIR_ITERATOR_RECURSIVE) ) {
		
		// copy the pattern at the end by trick it so that it gets overwritten
		if ( (fs_->flags & DIR_ITERATOR_ALL_DIRECTORIES) == 0 ) {
			size_t used_data_pattern = fs_->chb.used;
			err_code = char_buff_add(&fs_->chb, fs_->name_filter);
			if ( err_code != FUNC_OK ) {
				return err_code;
			}
			fs_->chb.used = used_data_pattern;
		} else {
			fs_->chb.data[fs_->chb.used] = '*';
			fs_->chb.data[fs_->chb.used+1] = 0;
		}
		
		// start the search
		h_find = FindFirstFileEx(fs_->chb.data, FindExInfoStandard, &find_data,
             FindExSearchLimitToDirectories, NULL, 0);
        if ( h_find == INVALID_HANDLE_VALUE ) {
			return 0;
        }
        
        // for each directory
        do { if ( is_entry_directory (&find_data) ) {
			// append the name of the directory
			err_code = char_buff_add(&fs_->chb, find_data.cFileName);
			if ( err_code != FUNC_OK ) {
				break;
			}
			
			// inform the callback
			if ( (fs_->flags & DIR_ITERATOR_EXCLUDE_DIRECTORIES) == 0 ) {
				err_code = fs_->kb (
				    fs_->chb.data, 
				    fs_->chb.data+used_data+1, 
				    fs_->user_data,
				    0);
				if ( err_code != FUNC_OK ) {
					break;
				}
			}

			// and call this function again
			err_code = dir_iterator_win (fs_);
			if ( err_code != FUNC_OK ) {
				break;
			}
        } } while ( FindNextFile (h_find, &find_data) );
        
        // close the search and exit if error
        FindClose (h_find);
		if ( err_code != FUNC_OK ) {
			return err_code;
		}
	}
	
	// now loop in kids
	if ( (fs_->flags & DIR_ITERATOR_EXCLUDE_FILES) == 0 ) {
		
		// copy the pattern at the end by trick it so that it gets overwritten
		if ( (fs_->flags & DIR_ITERATOR_ALL_FILES) == 0 ) {
			size_t used_data_pattern = fs_->chb.used;
			err_code = char_buff_add(&fs_->chb, fs_->name_filter);
			if ( err_code != FUNC_OK ) {
				return err_code;
			}
			fs_->chb.used = used_data_pattern;
		} else {
			fs_->chb.data[fs_->chb.used] = '*';
			fs_->chb.data[fs_->chb.used+1] = 0;
		}
		
		// start the search
		h_find = FindFirstFileEx(fs_->chb.data, FindExInfoStandard, &find_data,
             FindExSearchNameMatch, NULL, 0);
        if ( h_find == INVALID_HANDLE_VALUE ) {
			return 0;
        }
        
        // for each file
        do { if ( is_entry_file (&find_data) ) {
			// append the name of the file
			err_code = char_buff_add(&fs_->chb, find_data.cFileName);
			if ( err_code != FUNC_OK ) {
				break;
			}
			
			// inform the callback
			err_code = fs_->kb (
			    fs_->chb.data, 
				fs_->chb.data+used_data+1, 
				fs_->user_data,
				1);
			if ( err_code != FUNC_OK ) {
				break;
			}
			
        } } while ( FindNextFile (h_find, &find_data) );
        
        // close the search and exit if error
        FindClose (h_find);
		if ( err_code != FUNC_OK ) {
			return err_code;
		}
	}
	
	// restore the buffer to its former glorry
	fs_->chb.used = used_data;
	fs_->chb.data[used_data] = 0;
	
	return FUNC_OK;
}
#else // AITOWN_WIN32

static inline int is_dot_dot (struct dirent *dent) {
	for (;;) {
		if ( dent->d_name[0] == '.' ) {
			if ( dent->d_name[1] == 0 ) {
				break;
			} else if ( dent->d_name[1] == '.' ) {
				if ( dent->d_name[2] == 0 )
					break;
			}
		}
		return 0;
	}
	return 1;
}

func_error_t dir_iterator_unix (find_struct_t* fs_)
{
	DIR *dir;
	struct dirent *dent;
	struct stat st;
	size_t preserve_used;
	const char * file_name;
	int err_code = FUNC_OK;
	
	// we want to return the buffer in same state as we've got it
	size_t used_data = fs_->chb.used;
	{ // don't use end_of_dir below, the buffer may be rellocated
		char *end_of_dir = fs_->chb.data + fs_->chb.used;
		*end_of_dir = fs_->path_separator; end_of_dir++; fs_->chb.used++;
		*end_of_dir = 0;
	}
	
	// open the directory
	dir = opendir (fs_->chb.data);
	if (dir == NULL) {
		return FUNC_GENERIC_ERROR;
	}
	preserve_used = fs_->chb.used;
	
	// loop in all entries except . and .., symlinks
	while ( (dent = readdir (dir)) ) {
		fs_->chb.used = preserve_used;
		if ( is_dot_dot (dent) )
			continue;
		
		// copy this name after the path; trick it so that it does not increase
		err_code = char_buff_add(&fs_->chb, dent->d_name);
		if ( err_code != FUNC_OK ) {
			break;
		}
		file_name = fs_->chb.data + preserve_used;
		
		// get file info; if symbolic link, don't follow
		if (lstat (fs_->chb.data, &st) == -1)
			continue; // silently ignoring the error
		if (S_ISLNK(st.st_mode))
			continue;
		
		if (S_ISDIR(st.st_mode)) {
			// pattern match if they don't get all included
			if ( (fs_->flags & DIR_ITERATOR_ALL_DIRECTORIES) == 0 ) {
				if (regexec (&fs_->rex, file_name, 0, 0, 0))
					continue;
			}
			
			// inform the callback
			if ( (fs_->flags & DIR_ITERATOR_EXCLUDE_DIRECTORIES) == 0 ) {
				err_code = fs_->kb (fs_->chb.data, file_name, fs_->user_data, 0);
				if ( err_code != FUNC_OK ) {
					break;
				}
			}
			
			// recursive? dive in...
			if ( (fs_->flags & DIR_ITERATOR_RECURSIVE) ) {
				err_code = dir_iterator_unix(fs_);
				if ( err_code != FUNC_OK ) {
					break;
				}
			}
		} else if ( (fs_->flags & DIR_ITERATOR_EXCLUDE_FILES) == 0 ) {
		
			// pattern match
			if ( (fs_->flags & DIR_ITERATOR_ALL_FILES) == 0 ) {
				if (regexec (&fs_->rex, file_name, 0, 0, 0)) {
					continue;
				}
			}
			
			// inform the callback
			err_code = fs_->kb (fs_->chb.data, file_name, fs_->user_data, 1);
			if ( err_code != FUNC_OK ) {
				break;
			}
		}
	}
	
	if (dir)
		closedir(dir);
	// restore the buffer to its former glorry
	fs_->chb.used = used_data;
	fs_->chb.data[used_data] = 0;
	
	return FUNC_OK;
}
#endif // AITOWN_WIN32

func_error_t dir_iterator (const char *path_, const char *name_filter_, 
    dir_iterator_flags_t flags_, dir_iterator_foreach_t kb_, void *user_data_)
{
	find_struct_t	fs;
	fs.init_path = path_;
	fs.init_path_sz = strlen (path_);
	fs.name_filter = name_filter_;
	fs.kb = kb_;
	fs.flags = flags_;
	fs.user_data = user_data_;
	
	// create a buffer containing a copy of the path
	char_buff_init (&fs.chb, 1024);
	if ( fs.chb.data == NULL )
		return FUNC_MEMORY_ERROR;
	char_buff_add_string (&fs.chb, path_,fs.init_path_sz);
	
#ifdef AITOWN_WIN32
	fs.path_separator = '\\';
	return dir_iterator_win(&fs);
#else
	
	fs.path_separator = '/';
	char pattern_bf[256];
	char *actual_pattern = pattern_bf;
	int b_dyn_pattern = 0;
	int orig_len = strlen (name_filter_);
	int i;
	int j;
	int err_code;
	
	if ( orig_len == 0 ) {
		// no pattern means all files
		orig_len = 1;
		pattern_bf[0] = '.';
		pattern_bf[1] = '*';
		pattern_bf[2] = 0;
	} else {
		// only use dynamic buffer if we have to
		if ( orig_len > 128 ){
			actual_pattern = (char*)malloc (orig_len*2);
			b_dyn_pattern = 1;
		}
		
		// put proper regex codes
		j = 0;
		for ( i = 0; i < orig_len; i++ ) {
			char c = name_filter_[i];
			if (c == '*') {
				actual_pattern[j] = '.'; j++;
				actual_pattern[j] = '*'; j++;
			} else if (c == '?') {
				actual_pattern[j] = '.'; j++;
			} else if (c == ')') {
				actual_pattern[j] = '\\'; j++;
				actual_pattern[j] = ')'; j++;
			} else if (c == '(') {
				actual_pattern[j] = '\\'; j++;
				actual_pattern[j] = '('; j++;
			} else if (c == '.') {
				actual_pattern[j] = '\\'; j++;
				actual_pattern[j] = '.'; j++;
			} else {
				actual_pattern[j] = c; j++;
			}
		}
		actual_pattern[j] = 0;
	}
	
	// run the search
	for (;;) {
		err_code = regcomp (&fs.rex, actual_pattern, REG_ICASE | REG_NOSUB);
		if (err_code) {
			err_code = FUNC_GENERIC_ERROR;
			break;
		}
		err_code = dir_iterator_unix (&fs);
		regfree (&fs.rex);
		break;
	}
	
	// free the buffer
	if (b_dyn_pattern) {
		free (actual_pattern);
	}
	return err_code;
#endif

}

/*  FUNCTIONS    =========================================================== */
//
//
//
//
/* ------------------------------------------------------------------------- */
/* ========================================================================= */


