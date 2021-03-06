#pragma once

#include "storage/country.hpp"
#include "storage/country_decl.hpp"

#include "platform/platform.hpp"

#include "geometry/region2d.hpp"

#include "coding/file_container.hpp"

#include "base/cache.hpp"

#include "std/mutex.hpp"
#include "std/unordered_map.hpp"
#include "std/type_traits.hpp"

namespace storage
{
// This class allows users to get information about country by point
// or by name.
//
// *NOTE* This class is thread-safe.
class CountryInfoGetter
{
public:
  // Identifier of a region (index in m_countries array).
  using TRegionId = size_t;
  using TRegionIdSet = vector<TRegionId>;

  CountryInfoGetter(bool isSingleMwm) : m_isSingleMwm(isSingleMwm) {}
  virtual ~CountryInfoGetter() = default;

  // Returns country file name without an extension for a country |pt|
  // belongs to. If there is no such country, returns an empty
  // string.
  TCountryId GetRegionCountryId(m2::PointD const & pt) const;

  // Returns vector of countries file names without an extension for
  // countries belong to |rect|. |rough| provides fast rough result
  // or a slower but more precise one.
  vector<TCountryId> GetRegionsCountryIdByRect(m2::RectD const & rect, bool rough) const;

  // Returns a list of country ids by a |pt| in mercator.
  // |closestCoutryIds| is filled with country ids of mwm which covers |pt| or close to it.
  // |closestCoutryIds| is not filled with country world.mwm country id and with custom mwm.
  // If |pt| is covered by a sea or a ocean closestCoutryIds may be left empty.
  void GetRegionsCountryId(m2::PointD const & pt, TCountriesVec & closestCoutryIds);

  // Returns info for a region |pt| belongs to.
  void GetRegionInfo(m2::PointD const & pt, CountryInfo & info) const;

  // Returns info for a country by id.
  void GetRegionInfo(TCountryId const & countryId, CountryInfo & info) const;

  // Return limit rects of USA:
  // 0 - continental part
  // 1 - Alaska
  // 2 - Hawaii
  void CalcUSALimitRect(m2::RectD rects[3]) const;

  // Calculates limit rect for all countries whose name starts with
  // |prefix|.
  m2::RectD CalcLimitRect(string const & prefix) const;
  // Returns limit rect for |countryId| (non-expandable node).
  // Returns bounding box in mercator coordinates if |countryId| is a country id of non-expandable node
  // and zero rect otherwise.
  m2::RectD GetLimitRectForLeaf(TCountryId const & leafCountryId) const;

  // Returns identifiers for all regions matching to correspondent |affiliation|.
  virtual void GetMatchedRegions(string const & affiliation, TRegionIdSet & regions) const;

  // Returns true when |pt| belongs to at least one of the specified
  // |regions|.
  bool IsBelongToRegions(m2::PointD const & pt, TRegionIdSet const & regions) const;

  // Returns true if there're at least one region with id equals to
  // |countryId|.
  bool IsBelongToRegions(TCountryId const & countryId, TRegionIdSet const & regions) const;

  void RegionIdsToCountryIds(TRegionIdSet const & regions, TCountriesVec & countries) const;

  // Clears regions cache.
  inline void ClearCaches() const { ClearCachesImpl(); }

  void InitAffiliationsInfo(TMappingAffiliations const * affiliations);

protected:
  CountryInfoGetter() = default;

  // Returns identifier of a first country containing |pt|.
  TRegionId FindFirstCountry(m2::PointD const & pt) const;

  // Invokes |toDo| on each country whose name starts with |prefix|.
  template <typename ToDo>
  void ForEachCountry(string const & prefix, ToDo && toDo) const;

  // Clears regions cache.
  virtual void ClearCachesImpl() const = 0;

  // Returns true when |pt| belongs to a country identified by |id|.
  virtual bool IsBelongToRegionImpl(size_t id, m2::PointD const & pt) const = 0;

  // Returns true when |rect| intersects a country identified by |id|.
  virtual bool IsIntersectedByRegionImpl(size_t id, m2::RectD const & rect) const = 0;

  // Returns true when the distance from |pt| to country identified by |id| less then |distance|.
  virtual bool IsCloseEnough(size_t id, m2::PointD const & pt, double distance) = 0;

  // @TODO(bykoianko): consider to get rid of m_countryIndex.
  // The possibility should be considered.
  // List of all known countries.
  vector<CountryDef> m_countries;
  // Maps all leaf country id (file names) to their indices in m_countries.
  unordered_map<TCountryId, TRegionId> m_countryIndex;

  TMappingAffiliations const * m_affiliations = nullptr;

  // Maps country file name without an extension to a country info.
  map<string, CountryInfo> m_id2info;

  // m_isSingleMwm == true if the system is currently working with single (small) mwms
  // and false otherwise.
  // @TODO(bykoianko) Init m_isSingleMwm correctly.
  bool m_isSingleMwm;
};

// This class reads info about countries from polygons file and
// countries file and caches it.
class CountryInfoReader : public CountryInfoGetter
{
public:
  // This is the proper way to obtain a CountryInfoReader because
  // it accounts for migration and such.
  static unique_ptr<CountryInfoGetter> CreateCountryInfoReader(Platform const & platform);

  // The older version. The polygons are read from a file that was
  // used at the time when routing and map data were in different files.
  // This is a legacy method and it is extremely unlikely that you need it in your code.
  static unique_ptr<CountryInfoGetter> CreateCountryInfoReaderTwoComponentMwms(
      Platform const & platform);

  // The newer version. Use this one after the migration to single-component
  // mwm files has been carried out.
  // This is a legacy method and it is extremely unlikely that you need it in your code.
  static unique_ptr<CountryInfoGetter> CreateCountryInfoReaderOneComponentMwms(
      Platform const & platform);

protected:
  CountryInfoReader(ModelReaderPtr polyR, ModelReaderPtr countryR);

  // CountryInfoGetter overrides:
  void ClearCachesImpl() const override;
  bool IsBelongToRegionImpl(size_t id, m2::PointD const & pt) const override;
  bool IsIntersectedByRegionImpl(size_t id, m2::RectD const & rect) const override;
  bool IsCloseEnough(size_t id, m2::PointD const & pt, double distance) override;

  template <typename TFn>
  typename result_of<TFn(vector<m2::RegionD>)>::type WithRegion(size_t id, TFn && fn) const;

  FilesContainerR m_reader;
  mutable my::Cache<uint32_t, vector<m2::RegionD>> m_cache;
  mutable mutex m_cacheMutex;
};

// This class allows users to get info about very simply rectangular
// countries, whose info can be described by CountryDef instances.
// It's needed for testing purposes only.
class CountryInfoGetterForTesting : public CountryInfoGetter
{
public:
  CountryInfoGetterForTesting() = default;
  CountryInfoGetterForTesting(vector<CountryDef> const & countries);

  void AddCountry(CountryDef const & country);

  // CountryInfoGetter overrides:
  void GetMatchedRegions(string const & affiliation, TRegionIdSet & regions) const override;

protected:
  // CountryInfoGetter overrides:
  void ClearCachesImpl() const override;
  bool IsBelongToRegionImpl(size_t id, m2::PointD const & pt) const override;
  bool IsIntersectedByRegionImpl(size_t id, m2::RectD const & rect) const override;
  bool IsCloseEnough(size_t id, m2::PointD const & pt, double distance) override;
};
}  // namespace storage
