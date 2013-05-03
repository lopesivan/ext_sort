#pragma once

#include <string>

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "log4cpp/Category.hh"

#include "common/record_info.h"
#include "input_buffer.h"
#include "output_buffer.h"

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

  void merge_files(const std::string& data_file, size_t ram_size)
  {
    BOOST_ASSERT(ram_size > 0); // FIXME: какое-то более сильное условие должно быть
    BOOST_ASSERT(!m_files.empty());

    size_t partial_ram = ram_size/(m_files.size() + 1);
    boost::ptr_vector<input_buffer_t> ibs; 
    BOOST_FOREACH(const std::string& fname, m_files) {
      ibs.push_back(new input_buffer_t(fname, partial_ram));
      ibs.back().load_data();
    }

    std::string out_fname = "result.dat"; // FIXME: из параметров
    output_buffer_t ob(out_fname, data_file, ram_size - m_files.size()*partial_ram);

    while (ibs.size() > 0) {
      size_t min_idx = 0;
      record_info_t min_rec = ibs[0].peek();
      for (size_t i = 1; i < ibs.size(); ++i) {
        if (ibs[i].peek() < min_rec) {
          min_rec = ibs[i].peek();
          min_idx = i;
        }
      }

      ibs[min_idx].pop();

      ob.add(min_rec);

      if (!ibs[min_idx].has_cached_data()) {
        m_logger.debug("No cached data in file with idx %d", min_idx);
        ibs[min_idx].load_data();

        if (!ibs[min_idx].has_cached_data()) {
          m_logger.debug("Still no cached data, means file is exhausted, removing");
          ibs.erase(ibs.begin() + min_idx);
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
