#ifndef MAP_API_CACHE_BASE_H_
#define MAP_API_CACHE_BASE_H_
#include <string>

namespace dmap {

/**
 * Allows transactions to register caches without needing to know the
 * types of a templated cache.
 */
class CacheBase {
  friend class Transaction;

 public:
  virtual ~CacheBase();

 private:
  virtual std::string underlyingTableName() const = 0;
  virtual void prepareForCommit() = 0;
  // This is necessary after a commit (and after chunk tracking resolution!) in
  // order to re-fetch
  // the correct metadata from the database.
  virtual void discardCachedInsertions() = 0;
  virtual void refreshAvailableIds() = 0;
  virtual size_t size() const = 0;
};

}  // namespace dmap

#endif  // MAP_API_CACHE_BASE_H_
