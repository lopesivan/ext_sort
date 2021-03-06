#pragma once

#include <string>

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "log4cpp/Category.hh"

#include "input_file.hpp"
#include "output_buffer.hpp"

// FIXME: может не работать в кросс-платформенном варианте, но boost::filesystem ради этого дёргать пока не будем
static const std::string s_fname_template = "%s/tmp_%04d.dat";

class file_merger_t {
public:
  file_merger_t(const std::string& outfolder)
    : m_logger(log4cpp::Category::getRoot())
    , m_folder(outfolder)
    , m_formatter(s_fname_template) {}

  const std::string& next_file()
  {
    m_files.push_back((m_formatter % m_folder % m_files.size()).str());
    return m_files.back();
  }

  void merge_files(const std::string& out_fname, size_t ram_size)
  {
    BOOST_ASSERT(ram_size >= sizeof(record_t)*(m_files.size() + 1));
    BOOST_ASSERT(!m_files.empty());

    if (m_files.size() == 1) {
      m_logger.debug("Only one file to merge (%s), just renaming it", m_files.front().c_str());
      if (std::rename(m_files.front().c_str(), out_fname.c_str()) != 0) {
          m_logger.errorStream() << "Unable to rename " << m_files.front() <<
            " to " << out_fname << ". Sorted file is available as " << m_files.front();
      }
      return;
    }

    size_t partial_ram = ram_size/(m_files.size() + 1);
    boost::ptr_vector<input_file_t> infiles; 
    BOOST_FOREACH(const std::string& fname, m_files) {
      infiles.push_back(new input_file_t(fname, partial_ram, true));
      infiles.back().buffer().load_data();
    }

    output_buffer_t ob(out_fname, ram_size - m_files.size()*partial_ram);

    while (!infiles.empty()) {
      size_t min_idx = 0;
      const record_t* min_rec = infiles[0].buffer().peek();
      for (size_t i = 1; i < infiles.size(); ++i) {
        if (*infiles[i].buffer().peek() < *min_rec) {
          min_rec = infiles[i].buffer().peek();
          min_idx = i;
        }
      }

      ob.add(min_rec);
      input_buffer_t& inbuf = infiles[min_idx].buffer();
      inbuf.pop();

      if (!inbuf.has_cached_data()) {
        m_logger.debug("No cached data in file with idx %d", min_idx);
        inbuf.load_data();

        if (!inbuf.has_cached_data()) {
          m_logger.debug("Still no cached data, means file is exhausted, removing");
          infiles.erase(infiles.begin() + min_idx);
        }
      }
    }
    ob.dump();
  }

private:
  log4cpp::Category& m_logger;
  std::string m_folder;
  boost::format m_formatter;
  std::vector<std::string> m_files;
};
