/*
  Copyright (c) 2016, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "config.hpp"
#include "errno.hpp"
#include "fileinfo.hpp"
#include "fs_base_open.hpp"
#include "fs_cow.hpp"
#include "fs_path.hpp"
#include "policy_cache.hpp"
#include "ugid.hpp"

#include <fuse.h>

#include <string>
#include <vector>

using std::string;
using std::vector;
typedef Config::CacheFiles CacheFiles;

namespace l
{
  /*
    The kernel expects being able to issue read requests when running
    with writeback caching enabled so we must change O_WRONLY to
    O_RDWR.

    With writeback caching enabled the kernel handles O_APPEND. Could
    be an issue if the underlying file changes out of band but that is
    true of any caching.
  */
  static
  void
  tweak_flags_writeback_cache(int *flags_)
  {
    if((*flags_ & O_ACCMODE) == O_WRONLY)
      *flags_ = ((*flags_ & ~O_ACCMODE) | O_RDWR);
    if(*flags_ & O_APPEND)
      *flags_ &= ~O_APPEND;
  }

  static
  void
  config_to_ffi_flags(const Config   &config_,
                      fuse_file_info *ffi_)
  {
    switch(config_.cache_files)
      {
      case CacheFiles::ENUM::LIBFUSE:
        ffi_->direct_io  = config_.direct_io;
        ffi_->keep_cache = config_.kernel_cache;
        ffi_->auto_cache = config_.auto_cache;
        break;
      case CacheFiles::ENUM::OFF:
        ffi_->direct_io  = 1;
        ffi_->keep_cache = 0;
        ffi_->auto_cache = 0;
        break;
      case CacheFiles::ENUM::PARTIAL:
        ffi_->direct_io  = 0;
        ffi_->keep_cache = 0;
        ffi_->auto_cache = 0;
        break;
      case CacheFiles::ENUM::FULL:
        ffi_->direct_io  = 0;
        ffi_->keep_cache = 1;
        ffi_->auto_cache = 0;
        break;
      case CacheFiles::ENUM::AUTO_FULL:
        ffi_->direct_io  = 0;
        ffi_->keep_cache = 0;
        ffi_->auto_cache = 1;
        break;
      }
  }

  static
  int
  open_core(const string &basepath_,
            const char   *fusepath_,
            const int     flags_,
            const bool    link_cow_,
            uint64_t     *fh_)
  {
    int fd;
    string fullpath;

    fullpath = fs::path::make(basepath_,fusepath_);

    if(link_cow_ && fs::cow::is_eligible(fullpath.c_str(),flags_))
      fs::cow::break_link(fullpath.c_str());

    fd = fs::open(fullpath,flags_);
    if(fd == -1)
      return -errno;

    *fh_ = reinterpret_cast<uint64_t>(new FileInfo(fd,fusepath_,flags_));

    return 0;
  }

  static
  int
  open(Policy::Func::Search  searchFunc_,
       PolicyCache          &cache,
       const Branches       &branches_,
       const uint64_t        minfreespace_,
       const char           *fusepath_,
       const int             flags_,
       const bool            link_cow_,
       uint64_t             *fh_)
  {
    int rv;
    string basepath;

    rv = cache(searchFunc_,branches_,fusepath_,minfreespace_,&basepath);
    if(rv == -1)
      return -errno;

    return l::open_core(basepath,fusepath_,flags_,link_cow_,fh_);
  }
}

namespace FUSE
{
  int
  open(const char     *fusepath_,
       fuse_file_info *ffi_)
  {
    const fuse_context *fc     = fuse_get_context();
    const Config       &config = Config::ro();
    const ugid::Set     ugid(fc->uid,fc->gid);

    l::config_to_ffi_flags(config,ffi_);

    if(config.writeback_cache)
      l::tweak_flags_writeback_cache(&ffi_->flags);

    return l::open(config.func.open.policy,
                   config.open_cache,
                   config.branches,
                   config.minfreespace,
                   fusepath_,
                   ffi_->flags,
                   config.link_cow,
                   &ffi_->fh);
  }
}
