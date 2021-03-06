#pragma once

#include <cstdio>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <boost/foreach.hpp>
#include <boost/throw_exception.hpp>
#include <boost/noncopyable.hpp>

#include <log4cpp/Category.hh>

#include "common/record.hpp"

//! Описание записи в файле в виде ключ + смещение
class record_info_t {
public:
  explicit record_info_t() {}

  record_info_t(const record_t& record, uint64_t offset)
  {
    std::memcpy(key, record.key, 64);
    this->offset = offset;
  }

  bool operator< (const record_info_t& rhs) const
  {
    for (size_t i = 0; i < 64; ++i) {
      if (key[i] == rhs.key[i]) continue;
      return key[i] < rhs.key[i];
    }
    return false;
  }

  unsigned char key[64];
  uint64_t offset;
};

//! Класс для сравнения оригинального и сортированного файлов
class file_comparer_t: boost::noncopyable {
public:
  file_comparer_t(const std::string& fname_orig = "original.test.dat", const std::string& fname_sort = "sorted.test.dat")
    : m_logger(log4cpp::Category::getRoot())
    , m_fname_orig(fname_orig)
    , m_fname_sort(fname_sort) {}

  ~file_comparer_t()
  {
    if (remove(m_fname_orig.c_str()) != 0)
      m_logger.errorStream() << "Error while deleting file " << m_fname_orig;
    if (remove(m_fname_sort.c_str()) != 0)
      m_logger.errorStream() << "Error while deleting file " << m_fname_sort;
  }

  const std::string& fname_orig() { return m_fname_orig; }

  const std::string& fname_sort() { return m_fname_sort; }

  //! Проверка на то, что после сортировки файл отсортирован по ключам
  bool file_is_sorted()
  {
    for (size_t i = 0; i < rec_sort()->size() - 1; ++i) {
      if (!(rec_sort()->at(i) < rec_sort()->at(i + 1))) {
        return false;
      }
    }
    return true;
  }

  //! Проверка на то, что списки ключей оригинального/сортированного файлов совпадают
  bool records_are_equal()
  {
    if (rec_sort()->size() != rec_orig()->size()) {
      m_logger.warn("Record count in files differ");
    }

    for (size_t i = 0; i < rec_orig()->size(); ++i) {
      if (!keys_are_equal(rec_orig()->at(i), rec_sort()->at(i))) {
        m_logger.warn("Keys differ at index %u", i);
        return false;
      }
    }
    return true;
  }

  //! Проверка на то, что совпадают данные для элементов с max/min ключом
  bool random_entries_are_equal()
  {
    return (find_original_record(rec_sort()->front()) && find_original_record(rec_sort()->back()));
  }

private:
  typedef boost::shared_ptr<std::vector<record_info_t> > record_list_t;
  typedef std::vector<record_info_t>::iterator record_iterator_t;

  record_list_t records_from_file(const std::string& fname)
  {
    std::ifstream is(fname.c_str(), std::ifstream::binary);
    record_list_t result(new std::vector<record_info_t>());
    while (is.good())
    {
      record_t cur_test;
      size_t offset = is.tellg();
      is.read(reinterpret_cast<char*>(&cur_test), sizeof(cur_test));
      std::streamsize bytes_read = is.gcount();
      if (bytes_read != sizeof(cur_test))
        BOOST_THROW_EXCEPTION(std::runtime_error("Unable to read test struct from input file"));
      is.ignore(cur_test.size);
      m_logger.debugStream() << fname << ": " << cur_test;
      result->push_back(record_info_t(cur_test, offset));
    }
    return result;
  }

  record_list_t rec_orig()
  {
    if (!m_rec_orig) {
      m_logger.info("Loading records from original file");
      m_rec_orig = records_from_file(m_fname_orig);
      m_logger.info("Original file record count: %u", m_rec_orig->size());
      if (m_rec_orig->empty())
        m_logger.warn("No entries found in original file");
      std::sort(m_rec_orig->begin(), m_rec_orig->end());
    }
    return m_rec_orig;
  }

  record_list_t rec_sort()
  {
    if (!m_rec_sort) {
      m_logger.info("Loading records from sorted file");
      m_rec_sort = records_from_file(m_fname_sort);
      m_logger.info("Sorted file record count: %u", m_rec_sort->size());
      if (m_rec_sort->empty())
        m_logger.warn("No entries found in sorted file");
    }
    return m_rec_sort;
  }

  static bool keys_are_equal(const record_info_t& first, const record_info_t& second)
  {
    return std::memcmp(first.key, second.key, 64) == 0;
  }

  boost::shared_array<char> get_data(const std::string& fname, const record_info_t& ri, size_t& data_size)
  {
    m_logger.debug("Getting data from file %s for key %02x %02x %02x %02x",
      fname.c_str(), ri.key[0], ri.key[1], ri.key[2], ri.key[3]);
    io::stream<io::file_source> fin(fname);
    fin.seekg(ri.offset);
    record_t rec;
    fin.read(reinterpret_cast<char*>(&rec), sizeof(record_t));
    if (fin.gcount() != sizeof(record_t))
      BOOST_THROW_EXCEPTION(std::runtime_error("Unable to read record from file"));
    data_size = rec.size;
    boost::shared_array<char> res(new char[data_size]);
    fin.read(res.get(), data_size);
    if (fin.gcount() != data_size)
      BOOST_THROW_EXCEPTION(std::runtime_error("Unable to read data from file"));
    m_logger.debug("Data found (%u bytes), first bytes as uint32_t: %08x", data_size, *reinterpret_cast<uint32_t*>(res.get()));
    return res;
  }

  bool find_original_record(const record_info_t& ri)
  {
    size_t length_sort;
    boost::shared_array<char> ri_data = get_data(m_fname_sort, ri, length_sort);
    record_iterator_t it = std::find_if(rec_orig()->begin(), rec_orig()->end(), boost::bind(keys_are_equal, _1, ri));
    while (it != rec_orig()->end()) {
      size_t length_orig;
      boost::shared_array<char> data = get_data(m_fname_orig, *it, length_orig);
      if ((length_orig == length_sort) && (std::memcmp(data.get(), ri_data.get(), length_orig) == 0))
          return true;
      ++it;
      it = std::find_if(it, rec_orig()->end(), boost::bind(&file_comparer_t::keys_are_equal, _1, ri));
    }
    return false;
  }

private:
  log4cpp::Category& m_logger;
  std::string m_fname_orig;
  std::string m_fname_sort;
  record_list_t m_rec_orig;
  record_list_t m_rec_sort;
};
