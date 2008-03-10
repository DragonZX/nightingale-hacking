/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

#include "sbLocalDatabaseTreeView.h"
#include "sbPlaylistTreeSelection.h"
#include "sbFilterTreeSelection.h"

#include <nsIAtom.h>
#include <nsIAtomService.h>
#include <nsIClassInfoImpl.h>
#include <nsIDOMElement.h>
#include <nsIObjectOutputStream.h>
#include <nsIObjectInputStream.h>
#include <nsIProgrammingLanguage.h>
#include <nsIStringBundle.h>
#include <nsIStringEnumerator.h>
#include <nsITreeBoxObject.h>
#include <nsITreeColumns.h>
#include <nsIVariant.h>
#include <sbIClickablePropertyInfo.h>
#include <sbILocalDatabaseLibrary.h>
#include <sbILocalDatabasePropertyCache.h>
#include <sbILibrary.h>
#include <sbILibraryConstraints.h>
#include <sbIMediaListView.h>
#include <sbIMediaList.h>
#include <sbIMediaItem.h>
#include <sbIPropertyArray.h>
#include <sbIPropertyManager.h>
#include <sbISortableMediaListView.h>
#include <sbITreeViewPropertyInfo.h>

#include <nsComponentManagerUtils.h>
#include <nsMemory.h>
#include <nsServiceManagerUtils.h>
#include <nsThreadUtils.h>
#include <nsUnicharUtils.h>
#include <nsCoord.h>
#include <prlog.h>
#include "sbLocalDatabasePropertyCache.h"
#include "sbLocalDatabaseCID.h"
#include "sbLocalDatabaseGUIDArray.h"
#include "sbLocalDatabaseMediaItem.h"
#include "sbLocalDatabaseMediaListView.h"
#include <sbLibraryCID.h>
#include <sbPropertiesCID.h>
#include <sbStandardProperties.h>
#include <sbStringUtils.h>
#include <sbTArrayStringEnumerator.h>

#ifdef DEBUG
#include <prprf.h>
#endif

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbLocalDatabaseTreeView:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gLocalDatabaseTreeViewLog = nsnull;
#define TRACE(args) PR_LOG(gLocalDatabaseTreeViewLog, PR_LOG_DEBUG, args)
#define LOG(args)   PR_LOG(gLocalDatabaseTreeViewLog, PR_LOG_WARN, args)
#else
#define TRACE(args) /* nothing */
#define LOG(args)   /* nothing */
#endif

#define PROGRESS_VALUE_UNSET -1
#define PROGRESS_VALUE_COMPLETE 101

#define SB_STRING_BUNDLE_CHROME_URL "chrome://songbird/locale/songbird.properties"

#define BAD_CSS_CHARS "/.:# !@$%^&*(),?;'\"<>~=+`\\|[]{}"

/*
 * There are two distinct coordinate systems used in this file, one for the
 * underlying GUID array and one for the rows of the tree.  The term "index"
 * always refers to an index into the guid array, and the term "length" refers
 * to the length of the guid array.  The term "row" always refers to a row
 * in the tree, and the term "row count" refers to the total number of rows
 * in the tree.
 *
 * The TreeToArray and ArrayToTree methods are used to convert between the
 * two coordinate systems.
 *
 * mRowCache and mDiryRowCache use GUID array indexes for the keys.
 */


/* static */ nsresult PR_CALLBACK
sbLocalDatabaseTreeView::SelectionListSavingEnumeratorCallback(PRUint32 aIndex,
                                                               const nsAString& aId,
                                                               const nsAString& aGuid,
                                                               void* aUserData)
{
  NS_ENSURE_ARG_POINTER(aUserData);

  sbSelectionList* list = static_cast<sbSelectionList*>(aUserData);
  NS_ENSURE_STATE(list);

  nsAutoString guid(aGuid);
  PRBool success = list->Put(aId, guid);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

class sbAutoSelectEventsSuppressed
{
public:
  sbAutoSelectEventsSuppressed(nsITreeSelection* aSelection) :
    mSelection(aSelection),
    mSelectEventsSuppressed(PR_FALSE)
  {
  }

  ~sbAutoSelectEventsSuppressed()
  {
    if (mSelectEventsSuppressed) {
#ifdef DEBUG
      nsresult rv =
#endif
      set(PR_FALSE);
      NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to unset");
    }
  }

  nsresult set(PRBool aSelectEventsSuppressed)
  {
    if (mSelection) {
      mSelectEventsSuppressed = aSelectEventsSuppressed;
      return mSelection->SetSelectEventsSuppressed(aSelectEventsSuppressed);
    }
    else {
      return NS_OK;
    }
  }

private:
  nsITreeSelection* mSelection;
  PRBool mSelectEventsSuppressed;
};

NS_IMPL_ISUPPORTS9(sbLocalDatabaseTreeView,
                   nsIClassInfo,
                   nsISupportsWeakReference,
                   nsITreeView,
                   sbILocalDatabaseAsyncGUIDArrayListener,
                   sbILocalDatabaseGUIDArrayListener,
                   sbILocalDatabaseTreeView,
                   sbIMediaListViewTreeView,
                   sbIPlaylistPlaybackListener,
                   sbIMediaListViewSelectionListener)

NS_IMPL_CI_INTERFACE_GETTER8(sbLocalDatabaseTreeView,
                             nsIClassInfo,
                             nsITreeView,
                             sbILocalDatabaseAsyncGUIDArrayListener,
                             sbILocalDatabaseGUIDArrayListener,
                             sbILocalDatabaseTreeView,
                             sbIMediaListViewTreeView,
                             sbIPlaylistPlaybackListener,
                             sbIMediaListViewSelectionListener)

sbLocalDatabaseTreeView::sbLocalDatabaseTreeView() :
 mListType(eLibrary),
 mMediaListView(nsnull),
 mViewSelection(nsnull),
 mCachedRowCount(0),
 mNextGetByIndexAsync(-1),
 mManageSelection(PR_FALSE),
 mMouseState(sbILocalDatabaseTreeView::MOUSE_STATE_NONE),
 mMouseStateRow(-1),
 mSelectionIsAll(PR_FALSE),
 mCachedRowCountDirty(PR_TRUE),
 mCachedRowCountPending(PR_FALSE),
 mIsArrayBusy(PR_FALSE),
 mGetByIndexAsyncPending(PR_FALSE),
 mClearSelectionPending(PR_FALSE),
 mFakeAllRow(PR_FALSE),
 mSelectionChanging(PR_FALSE),
 mIsListeningToPlayback(PR_FALSE)
{
#ifdef PR_LOGGING
  if (!gLocalDatabaseTreeViewLog) {
    gLocalDatabaseTreeViewLog = PR_NewLogModule("sbLocalDatabaseTreeView");
  }
#endif
}

sbLocalDatabaseTreeView::~sbLocalDatabaseTreeView()
{
  nsresult rv;
  if (mArray) {
    nsCOMPtr<sbILocalDatabaseAsyncGUIDArrayListener> asyncListener =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseTreeView*, this),
                        &rv);
    if (NS_SUCCEEDED(rv))
      mArray->RemoveAsyncListener(asyncListener);
  }

  if (mViewSelection) {
    nsCOMPtr<sbIMediaListViewSelectionListener> selectionListener =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseTreeView*, this),
                        &rv);
    if (NS_SUCCEEDED(rv))
      mViewSelection->RemoveListener(selectionListener);
  }

  NS_ASSERTION(!mIsListeningToPlayback, "Still listening when dtor called");
  // declare ourselves dead, in case the async listener is actually in the
  // process of waiting to fire. yay.
  ClearWeakReferences();
}

nsresult
sbLocalDatabaseTreeView::Init(sbLocalDatabaseMediaListView* aMediaListView,
                              sbILocalDatabaseAsyncGUIDArray* aArray,
                              sbIPropertyArray* aCurrentSort,
                              sbLocalDatabaseTreeViewState* aState)
{
  NS_ENSURE_ARG_POINTER(aMediaListView);
  NS_ENSURE_ARG_POINTER(aArray);

  nsresult rv;

  // Make sure we actually are getting a sort or a state
  if (aCurrentSort) {
    NS_ENSURE_TRUE(!aState, NS_ERROR_INVALID_ARG);
    PRUint32 arrayLength;
    rv = aCurrentSort->GetLength(&arrayLength);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_STATE(arrayLength);
  }
  else {
    NS_ENSURE_ARG_POINTER(aState);
  }

  mPropMan = do_GetService(SB_PROPERTYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  mMediaListView = aMediaListView;
  mArray = aArray;

  // Determine the list type
  PRBool isDistinct;
  rv = mArray->GetIsDistinct(&isDistinct);
  NS_ENSURE_SUCCESS(rv, rv);

  if (isDistinct) {
    mListType = eDistinct;
    mManageSelection = PR_TRUE;
    mFakeAllRow = PR_TRUE;
    mSelectionIsAll = PR_TRUE;
  }
  else {
    mManageSelection = PR_FALSE;
    nsString baseTable;
    rv = mArray->GetBaseTable(baseTable);
    NS_ENSURE_SUCCESS(rv, rv);

    if (baseTable.EqualsLiteral("media_items")) {
      mListType = eLibrary;
    }
    else {
      if (baseTable.EqualsLiteral("simple_media_lists")) {
        mListType = eSimple;
      }
      else {
        return NS_ERROR_UNEXPECTED;
      }
    }
  }

  // If we are not managing our selection, we rely on our view for selection
  // management.  Add a listener to the view's selection so we can track
  // changes
  if (!mManageSelection) {
    nsCOMPtr<sbIMediaListViewSelection> viewSelection;
    rv = mMediaListView->GetSelection(getter_AddRefs(viewSelection));
    NS_ENSURE_SUCCESS(rv, rv);
    mViewSelection = viewSelection;

    nsCOMPtr<sbIMediaListViewSelectionListener> selectionListener =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseTreeView*, this), &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mViewSelection->AddListener(selectionListener);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mArray->GetPropertyCache(getter_AddRefs(mPropertyCache));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILocalDatabaseGUIDArrayListener> listener =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseTreeView*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mArray->SetListener(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILocalDatabaseAsyncGUIDArrayListener> asyncListener =
    do_QueryInterface(NS_ISUPPORTS_CAST(sbILocalDatabaseTreeView*, this), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mArray->AddAsyncListener(asyncListener);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mArray->GetFetchSize(&mFetchSize);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success = mRowCache.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  success = mPageCacheStatus.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  success = mDirtyRowCache.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  success = mSelectionList.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  if (aState) {
#ifdef DEBUG
  {
    nsString buff;
    aState->ToString(buff);
    LOG(("sbLocalDatabaseTreeView::Init[0x%.8x] - restoring %s",
          this, NS_LossyConvertUTF16toASCII(buff).get()));
  }
#endif
    rv = aState->mSort->GetProperty(mCurrentSortProperty);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool isAscending;
    rv = aState->mSort->GetIsAscending(&isAscending);
    NS_ENSURE_SUCCESS(rv, rv);
    mCurrentSortDirectionIsAscending = isAscending;

    if (mManageSelection) {
      mSelectionIsAll = aState->mSelectionIsAll;
      if (!mSelectionIsAll) {
        aState->mSelectionList.EnumerateRead(SB_CopySelectionListCallback,
                                             &mSelectionList);
      }
    }
  }
  else {
    // Grab the top level sort property from the bag
    nsCOMPtr<sbIProperty> property;
    rv = aCurrentSort->GetPropertyAt(0, getter_AddRefs(property));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = property->GetId(mCurrentSortProperty);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString value;
    rv = property->GetValue(value);
    NS_ENSURE_SUCCESS(rv, rv);

    mCurrentSortDirectionIsAscending = value.EqualsLiteral("a");
  }

  // Get our localized strings
  nsCOMPtr<nsIStringBundleService> bundleService =
    do_GetService("@mozilla.org/intl/stringbundle;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIStringBundle> stringBundle;
  rv = bundleService->CreateBundle(SB_STRING_BUNDLE_CHROME_URL,
                                   getter_AddRefs(stringBundle));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stringBundle->GetStringFromName(NS_LITERAL_STRING("library.all").get(),
                                       getter_Copies(mLocalizedAll));
  if (NS_FAILED(rv)) {
    mLocalizedAll.AssignLiteral("library.all");
  }

  // If this is not a distinct list, we will want a listener to track playback
  // changes.  This listener should be set up in SetTree.
  if (mListType != eDistinct) {
    mPlaylistPlayback =
      do_GetService("@songbirdnest.com/Songbird/PlaylistPlayback;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::Rebuild()
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - Rebuild()", this));

  nsresult rv;

  // Clear the selection from the tree only if we don't have everything
  // selected.  The selection will be resored when the cached pages are read
  // back
  if (mManageSelection && mRealSelection && !mSelectionIsAll) {
    mClearSelectionPending = PR_TRUE;
  }

  // Invalidate our cache
  rv = InvalidateCache();
  NS_ENSURE_SUCCESS(rv, rv);

  // Force a row recount
  mCachedRowCountDirty = PR_TRUE;
  mCachedRowCountPending = PR_FALSE;

  PRInt32 count;
  rv = GetRowCount(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void
sbLocalDatabaseTreeView::ClearMediaListView()
{
  mMediaListView = nsnull;
  mViewSelection = nsnull;
}

/**
 * \brief Parses a string and separates space-delimited substrings into nsIAtom
 *        elements.
 *
 * Shamelessly adapted from nsTreeUtils.cpp.
 */
nsresult
sbLocalDatabaseTreeView::TokenizeProperties(const nsAString& aProperties,
                                            nsISupportsArray* aAtomArray)
{
  NS_ASSERTION(!aProperties.IsEmpty(), "Don't give this an empty string");
  NS_ASSERTION(aAtomArray, "Null pointer!");

  const PRUnichar* current, *end;
  aProperties.BeginReading(&current, &end);

  static const PRUnichar sSpaceChar = ' ';

  nsresult rv;
  nsCOMPtr<nsIAtomService> atomService =
    do_GetService(NS_ATOMSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  do {

    // Skip whitespace
    while (current < end && *current == sSpaceChar) {
      ++current;
    }

    // If only whitespace, we're done
    if (current == end) {
      break;
    }

    // Note the first non-whitespace character
    const PRUnichar* firstChar = current;

    // Advance to the next whitespace character
    while (current < end && *current != sSpaceChar) {
      ++current;
    }

    nsAutoString token(Substring(firstChar, current));

    // Make an atom
    nsCOMPtr<nsIAtom> atom;
    rv = atomService->GetAtom(token.get(), getter_AddRefs(atom));
    NS_ENSURE_SUCCESS(rv, rv);

    // Don't encourage people to step on each other's toes.
    if (aAtomArray->IndexOf(atom) != -1) {
      continue;
    }

    rv = aAtomArray->AppendElement(atom);
    NS_ENSURE_SUCCESS(rv, rv);

  } while (current < end);

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::UpdateRowCount(PRUint32 aRowCount)
{
  nsresult rv;

  if (mTreeBoxObject) {
    PRUint32 oldRowCount = mCachedRowCount;
    mCachedRowCount = aRowCount;
    mCachedRowCountDirty = PR_FALSE;
    mCachedRowCountPending = PR_FALSE;

    // Change the number of rows in the tree by the difference between the
    // new row count and the old cached row count.  We still need to invalidate
    // the whole tree since we don't know where the row changes too place.
    PRInt32 delta = mCachedRowCount - oldRowCount;
    rv = mTreeBoxObject->BeginUpdateBatch();
    NS_ENSURE_SUCCESS(rv, rv);
    if (delta != 0) {
      rv = mTreeBoxObject->RowCountChanged(oldRowCount, delta);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = mTreeBoxObject->Invalidate();
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mTreeBoxObject->EndUpdateBatch();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // This is the first method that gets called after the tree has been rebuilt.
  // If we have a fake all row and the selection is "all", select it here.
  // Note that we do this here rather than in the code that restores selection
  // because we never get a callback for this fake row when repopulating the
  // tree.
  if (mManageSelection && mRealSelection && mFakeAllRow &&
      mSelectionIsAll && mTreeBoxObject) {
    mSelectionChanging = PR_TRUE;
    rv = mRealSelection->Select(0);
    mSelectionChanging = PR_FALSE;
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetCellPropertyValue(PRInt32 aIndex,
                                              nsITreeColumn *aTreeColumn,
                                              nsAString& _retval)
{
  NS_ENSURE_ARG_POINTER(aTreeColumn);

  nsresult rv;

  nsAutoString bind;
  rv = GetPropertyForTreeColumn(aTreeColumn, bind);
  NS_ENSURE_SUCCESS(rv, rv);

  // If this is an ordinal column, return just the row number
  if (bind.EqualsLiteral(SB_PROPERTY_ORDINAL)) {
    _retval.AppendInt(aIndex + 1);
    return NS_OK;
  }

  nsCOMPtr<sbIPropertyInfo> info;
  rv = mPropMan->GetPropertyInfo(bind, getter_AddRefs(info));
  NS_ENSURE_SUCCESS(rv, rv);

  // Check our local map cache of property bags and return the cell value if
  // we have it cached
  nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
  if (mRowCache.Get(aIndex, getter_AddRefs(bag))) {
    nsAutoString value;
    rv = bag->GetProperty(bind, value);
    NS_ENSURE_SUCCESS(rv, rv);

    // Format the value for display
    rv = info->Format(value, _retval);
    if (NS_FAILED(rv)) {
      _retval.Truncate();
    }

    return NS_OK;
  }

  PageCacheStatus status;
  rv = GetPageCachedStatus(aIndex, &status);
  NS_ENSURE_SUCCESS(rv, rv);

  switch(status) {

    // Since we cache each page's data in mRowCache, this should never be
    // called.  Warn and fall through so we can still process the request.
    case eCached:
      NS_WARNING("Not cached in mRowCache but page cached?");

    // The age this row is in is not yet cached, so async request it and
    // return blank
    case eNotCached:
    {
      // Don't bother starting any requests for data while our row cache is
      // dirty since the result would be invalid
      if (!mCachedRowCountDirty) {

        // If we already have a pending request, don't start another, just put
        // this new request on deck.  If there is already a next request on deck,
        // clobber it and reset its page's cache status.  The purpose of this is
        // to prevent a lot of requests from getting stacked up if the user
        // scrolls the list around a lot
        if (mGetByIndexAsyncPending) {

          if (mNextGetByIndexAsync > -1) {
            rv = SetPageCachedStatus(mNextGetByIndexAsync, eNotCached);
            NS_ENSURE_SUCCESS(rv, rv);
          }

          mNextGetByIndexAsync = aIndex;
          rv = SetPageCachedStatus(aIndex, ePending);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        else {

          rv = SetPageCachedStatus(aIndex, ePending);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mArray->GetGuidByIndexAsync(aIndex);
          NS_ENSURE_SUCCESS(rv, rv);
          mGetByIndexAsyncPending = PR_TRUE;
        }
      }

      // Try our dirty cache so we can show something
      if (mDirtyRowCache.Get(aIndex, getter_AddRefs(bag))) {
        rv = bag->GetProperty(bind, _retval);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        _retval.Truncate();
      }
    }
    break;

    // The page this row is in is pending, so just return a blank
    case ePending:
    {
      // Try our dirty cache so we can show something
      if (mDirtyRowCache.Get(aIndex, getter_AddRefs(bag))) {
        nsString value;
        rv = bag->GetProperty(bind, _retval);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        _retval.Truncate();
      }
    }
    break;
  }

  if (!_retval.Equals(EmptyString())) {
    // Format the value for display
    rv = info->Format(_retval, _retval);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetPropertyBag(const nsAString& aGuid,
                                        PRUint32 aIndex,
                                        sbILocalDatabaseResourcePropertyBag** _retval)
{
  nsresult rv;

  // Since we've linked a property cache with the array, this guid
  // should be cached
  const PRUnichar* guid = aGuid.BeginReading();

  PRUint32 count = 0;
  sbILocalDatabaseResourcePropertyBag** bags = nsnull;
  rv = mPropertyCache->GetProperties(&guid, 1, &count, &bags);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
  if (count == 1 && bags[0]) {
    bag = bags[0];
  }
  NS_FREE_XPCOM_ISUPPORTS_POINTER_ARRAY(count, bags);

  if (!bag) {
    NS_WARNING("Could not find properties for guid!");
    return NS_ERROR_UNEXPECTED;
  }

  PRBool success = mRowCache.Put(aIndex, bag);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*_retval = bag);
  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetPageCachedStatus(PRUint32 aIndex,
                                             PageCacheStatus* aStatus)
{
  PRUint32 cell = aIndex / mFetchSize;
  PRUint32 status;
  if (mPageCacheStatus.Get(cell, &status)) {
    *aStatus = PageCacheStatus(status);
  }
  else {
    *aStatus = eNotCached;
  }

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - "
         "GetPageCachedStatus(%d) = %d", this, aIndex, *aStatus));

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::SetPageCachedStatus(PRUint32 aIndex,
                                             PageCacheStatus aStatus)
{
  if (aIndex >= mCachedRowCount) {
    NS_WARNING("Attempt to set page cache status on invalid row");
    return NS_OK;
  }

  PRUint32 cell = aIndex / mFetchSize;
  PRBool success = mPageCacheStatus.Put(cell, (PRUint32) aStatus);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - "
         "SetPageCachedStatus(%d, %d) cell = %d", this, aIndex, aStatus, cell));

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::InvalidateCache()
{
  // Copy the visible pages into our temporary dirty row cache so we have
  // something to show the user while the tree is rebuilding
  if (mTreeBoxObject) {
    PRInt32 first;
    PRInt32 last;
    nsresult rv = mTreeBoxObject->GetFirstVisibleRow(&first);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mTreeBoxObject->GetLastVisibleRow(&last);
    NS_ENSURE_SUCCESS(rv, rv);

    if (first >= 0 && last >= 0) {
      for (PRUint32 row = first; row <= (PRUint32) last; row++) {
        nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
        if (mRowCache.Get(TreeToArray(row), getter_AddRefs(bag))) {
          PRBool success = mDirtyRowCache.Put(TreeToArray(row), bag);
          NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
        }
      }
    }

  }
  mRowCache.Clear();
  mPageCacheStatus.Clear();

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::SaveSelectionList()
{
  NS_ASSERTION(mManageSelection,
               "SaveSelectionList() called but we're not managing selection");

  // If the current selection is everything, don't save anything
  if (mSelectionIsAll) {
    return NS_OK;
  }

  nsresult rv = EnumerateSelection(SelectionListSavingEnumeratorCallback,
                                   &mSelectionList);

  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


nsresult
sbLocalDatabaseTreeView::EnumerateSelection(sbSelectionEnumeratorCallbackFunc aFunc,
                                            void* aUserData)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - EnumerateSelection()", this));

  if (mRealSelection) {

    // Iterate through the selection's ranges and store the unique identifier
    // for each row in the selection hash table.  Note that we don't want
    // to clear the selection list first as to keep items that were previously
    // selected but were never re-selected by the background cache fill
    PRInt32 rangeCount;
    nsresult rv = mRealSelection->GetRangeCount(&rangeCount);
    NS_ENSURE_SUCCESS(rv, rv);

    for (PRInt32 i = 0; i < rangeCount; i++) {
      PRInt32 min;
      PRInt32 max;
      rv = mRealSelection->GetRangeAt(i, &min, &max);
      NS_ENSURE_SUCCESS(rv, rv);

      if (min >= 0 && max >= 0) {
        for (PRInt32 j = min; j <= max; j++) {

          if (IsAllRow(j)) {
            continue;
          }

          nsAutoString id;
          PRUint32 index = TreeToArray(j);
          rv = GetUniqueIdForIndex(index, id);
          NS_ENSURE_SUCCESS(rv, rv);

          nsAutoString guid;
          rv = mArray->GetGuidByIndex(index, guid);
          NS_ENSURE_SUCCESS(rv, rv);

          TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SaveSelectionList() - "
                 "saving %s from index %d guid %s",
                 this, NS_ConvertUTF16toUTF8(id).get(), index,
                 NS_ConvertUTF16toUTF8(guid).get()));

          rv = aFunc(index, id, guid, aUserData);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }
      else {
        NS_WARNING("Bad value returned from nsTreeSelection::GetRangeAt");
      }
    }

  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetUniqueIdForIndex(PRUint32 aIndex, nsAString& aId)
{
  nsresult rv;

  aId.Truncate();

  // For distinct lists, the sort key works as a unique id
  if (mListType == eDistinct) {
    rv = mArray->GetSortPropertyValueByIndex(aIndex, aId);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // For regular lists, the unique identifer is composed of the lists' guid
    // appended to the item's guid appeneded to the item's database rowid.
    nsCOMPtr<sbIMediaList> list;
    rv = mMediaListView->GetMediaList(getter_AddRefs(list));
    NS_ENSURE_SUCCESS(rv, rv);

    nsString guid;
    rv = list->GetGuid(guid);
    NS_ENSURE_SUCCESS(rv, rv);
    aId.Append(guid);
    aId.Append('|');

    guid.Truncate();
    rv = mArray->GetGuidByIndex(aIndex, guid);
    NS_ENSURE_SUCCESS(rv, rv);
    aId.Append(guid);
    aId.Append('|');

    PRUint64 rowid;
    rv = mArray->GetRowidByIndex(aIndex, &rowid);
    NS_ENSURE_SUCCESS(rv, rv);
    AppendInt(aId, rowid);
  }

  return NS_OK;
}

void
sbLocalDatabaseTreeView::SetSelectionIsAll(PRBool aSelectionIsAll) {

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetSelectionIsAll(%d)",
         this, aSelectionIsAll));
  NS_ASSERTION(mManageSelection,
               "SetSelectionIsAll() called but we're not managing selection");

  mSelectionIsAll = aSelectionIsAll;

  // An all selection means everything in our selection list was implicitly
  // selected so we can clear it
  if (mSelectionIsAll) {
    ClearSelectionList();
  }

}

void
sbLocalDatabaseTreeView::ClearSelectionList() {

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - ClearSelectionList()", this));
  NS_ASSERTION(mManageSelection,
               "ClearSelectionList() called but we're not managing selection");

  mSelectionList.Clear();
}

nsresult
sbLocalDatabaseTreeView::UpdateColumnSortAttributes(const nsAString& aProperty,
                                                    PRBool aDirection)
{
  nsresult rv;

  if (!mTreeBoxObject) {
    NS_WARNING("Unable to update column sort attributes, no box object!");
    return NS_OK;
  }

  nsCOMPtr<nsITreeColumns> columns;
  rv = mTreeBoxObject->GetColumns(getter_AddRefs(columns));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 count;
  rv = columns->GetCount(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRInt32 i = 0; i < count; i++) {
    nsCOMPtr<nsITreeColumn> column;
    rv = columns->GetColumnAt(i, getter_AddRefs(column));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!column) {
      NS_WARNING("Failed to find column!");
      continue;
    }

    nsCOMPtr<nsIDOMElement> element;
    rv = column->GetElement(getter_AddRefs(element));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString bind;
    rv = element->GetAttribute(NS_LITERAL_STRING("bind"), bind);
    NS_ENSURE_SUCCESS(rv, rv);

    if (bind.Equals(aProperty)) {
      rv = element->SetAttribute(NS_LITERAL_STRING("sortActive"),
                                 NS_LITERAL_STRING("true"));
      NS_ENSURE_SUCCESS(rv, rv);

      nsAutoString direction;
      if (aDirection) {
        direction.AssignLiteral("ascending");
      }
      else {
        direction.AssignLiteral("descending");
      }
      rv = element->SetAttribute(NS_LITERAL_STRING("sortDirection"),
                                 direction);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      rv = element->RemoveAttribute(NS_LITERAL_STRING("sortActive"));
      NS_ENSURE_SUCCESS(rv, rv);
      rv = element->RemoveAttribute(NS_LITERAL_STRING("sortDirection"));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetState(sbLocalDatabaseTreeViewState** aState)
{
  NS_ENSURE_ARG_POINTER(aState);

  nsresult rv;

  nsRefPtr<sbLocalDatabaseTreeViewState> state =
    new sbLocalDatabaseTreeViewState();
  NS_ENSURE_TRUE(state, NS_ERROR_OUT_OF_MEMORY);

  rv = state->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  state->mSort = do_CreateInstance(SONGBIRD_LIBRARYSORT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = state->mSort->Init(mCurrentSortProperty,
                          mCurrentSortDirectionIsAscending);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mSelectionIsAll) {
    state->mSelectionIsAll = PR_TRUE;
  }
  else {
    mSelectionList.EnumerateRead(SB_CopySelectionListCallback,
                                 &state->mSelectionList);
    rv = EnumerateSelection(SelectionListSavingEnumeratorCallback,
                            &state->mSelectionList);
    NS_ENSURE_SUCCESS(rv, rv);
  }

#ifdef DEBUG
  {
    nsString buff;
    state->ToString(buff);
    LOG(("sbLocalDatabaseTreeView::GetState[0x%.8x] - returning %s",
          this, NS_LossyConvertUTF16toASCII(buff).get()));
  }
#endif

  NS_ADDREF(*aState = state);
  return NS_OK;
}

// sbILocalDatabaseTreeView
NS_IMETHODIMP
sbLocalDatabaseTreeView::SetSort(const nsAString& aProperty, PRBool aDirection)
{
  LOG(("sbLocalDatabaseTreeView[0x%.8x] - SetSort(%s, %d)",
       this, NS_LossyConvertUTF16toASCII(aProperty).get(), aDirection));

  nsresult rv;

  nsCOMPtr<sbIMediaList> list;
  rv = mMediaListView->GetMediaList(getter_AddRefs(list));
  NS_ENSURE_SUCCESS(rv, rv);

  nsString isSortable;
  rv = list->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_ISSORTABLE), isSortable);
  NS_ENSURE_SUCCESS(rv, rv);

  if (isSortable.Equals(NS_LITERAL_STRING("0"))) {
    // list is not sortable
    return NS_ERROR_FAILURE;
  }

  // If this the library, a sort on ordinal is replaced with a create date
  // sort since the library does not have an ordinal.
  nsString sortProperty;
  sortProperty = aProperty;
  if (mListType == eLibrary && aProperty.EqualsLiteral(SB_PROPERTY_ORDINAL)) {
    sortProperty.AssignLiteral(SB_PROPERTY_CREATED);
  }

  // If we are linked to a media list view, use its interfaces to manage
  // the sort
  if (mListType != eDistinct) {
    NS_ENSURE_STATE(mMediaListView);

    nsCOMPtr<sbIPropertyArray> sort;
    rv = mPropMan->GetPropertySort(sortProperty,
                                   aDirection,
                                   getter_AddRefs(sort));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISortableMediaListView> sortable =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbISortableMediaListView*, mMediaListView), &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = sortable->SetSort(sort);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = mArray->ClearSorts();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mArray->AddSort(sortProperty, aDirection);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mArray->Invalidate();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = Rebuild();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mCurrentSortProperty = aProperty;
  mCurrentSortDirectionIsAscending = aDirection;

  rv = UpdateColumnSortAttributes(aProperty, aDirection);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetSelectionChanging(PRBool* aSelectionChanging)
{
  NS_ENSURE_ARG_POINTER(aSelectionChanging);

  *aSelectionChanging = mSelectionChanging;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::InvalidateRowsByGuid(const nsAString& aGuid)
{
  LOG(("sbLocalDatabaseTreeView[0x%.8x] - InvalidateRowsByGuid(%s)",
       this, NS_LossyConvertUTF16toASCII(aGuid).get()));

  if (mTreeBoxObject) {
    PRInt32 first;
    PRInt32 last;
    nsresult rv = mTreeBoxObject->GetFirstVisibleRow(&first);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mTreeBoxObject->GetLastVisibleRow(&last);
    NS_ENSURE_SUCCESS(rv, rv);

    if (first >= 0 && last >= 0) {
      PRUint32 length;
      rv = mArray->GetLength(&length);
      NS_ENSURE_SUCCESS(rv, rv);

      PRInt32 rowCount = ArrayToTree(length);

      if (last >= rowCount)
        last = rowCount - 1;

      for (PRInt32 row = first; row <= last; row++) {
        nsAutoString guid;
        rv = mArray->GetGuidByIndex(TreeToArray(row), guid);
        NS_ENSURE_SUCCESS(rv, rv);
        if (guid.Equals(aGuid)) {
          rv = mTreeBoxObject->InvalidateRow(row);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }
    }

  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::SetMouseState(PRInt32 aRow,
                                       nsITreeColumn* aColumn,
                                       PRUint32 aState)
{
#ifdef PR_LOGGING
  PRInt32 colIndex = -1;
  if (aColumn) {
    aColumn->GetIndex(&colIndex);
  }
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetMouseState(%d, %d, %d)", this,
         aRow, colIndex, aState));
#endif

  nsresult rv;

  if (mMouseState && mMouseStateRow >= 0 && mMouseStateColumn) {
    mMouseState = sbILocalDatabaseTreeView::MOUSE_STATE_NONE;
    if (mTreeBoxObject) {
      rv = mTreeBoxObject->InvalidateCell(mMouseStateRow, mMouseStateColumn);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  mMouseState       = aState;
  mMouseStateRow    = aRow;
  mMouseStateColumn = aColumn;

  if (mMouseStateRow >= 0 && mMouseStateColumn && mTreeBoxObject) {
    rv = mTreeBoxObject->InvalidateCell(mMouseStateRow, mMouseStateColumn);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// sbILocalDatabaseGUIDArrayListener
NS_IMETHODIMP
sbLocalDatabaseTreeView::OnBeforeInvalidate()
{
  if (mManageSelection) {
    nsresult rv = SaveSelectionList();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// sbILocalDatabaseAsyncGUIDArrayListener
NS_IMETHODIMP
sbLocalDatabaseTreeView::OnGetLength(PRUint32 aLength,
                                     nsresult aResult)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - OnGetLength(%d)", this, aLength));

  nsresult rv;

  if (NS_SUCCEEDED(aResult)) {
    rv = UpdateRowCount(ArrayToTree(aLength));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnGetGuidByIndex(PRUint32 aIndex,
                                          const nsAString& aGUID,
                                          nsresult aResult)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - OnGetGuidByIndex(%d, %s)", this,
         aIndex, NS_LossyConvertUTF16toASCII(aGUID).get()));

  nsresult rv;
  sbAutoSelectEventsSuppressed autoSelectEventsSuppressed(mRealSelection);

  mGetByIndexAsyncPending = PR_FALSE;

  // Ignore any outstanding callbacks while our row cache is dirty
  if (mCachedRowCountDirty) {
    return NS_OK;
  }

  if (NS_SUCCEEDED(aResult)) {
    // If there is a clear selection pending, clear the selection
    if (mManageSelection && mRealSelection && mClearSelectionPending) {
      mSelectionChanging = PR_TRUE;
      if (mSelectionIsAll) {
        rv = mRealSelection->Select(0);
      }
      else {
        rv = mRealSelection->ClearSelection();
      }
      mSelectionChanging = PR_FALSE;
      NS_ENSURE_SUCCESS(rv, rv);
      mClearSelectionPending = PR_FALSE;
    }

    // Now we know that the page this row is in has been fully cached by
    // the guid array, cache the entire page in our cache
    PRUint32 start = (aIndex / mFetchSize) * mFetchSize;
    PRUint32 end = start + mFetchSize - 1;
    if (end > TreeToArray(mCachedRowCount)) {
      end = TreeToArray(mCachedRowCount) - 1;
    }
    PRUint32 length = end - start + 1;

    // Suppress select events
    rv = autoSelectEventsSuppressed.set(PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);

    // Build an array of guids we want.  We build both a string array and a
    // pointer array because we need the former to make sure the pointers
    // remain valid.
    nsTArray<nsString> guids(length);
    nsTArray<const PRUnichar*> guidArray(length);

    for (PRUint32 i = 0; i < length; i++) {
      PRUint32 index = i + start;
      nsAutoString guid;
      rv = mArray->GetGuidByIndex(index, guid);
      if (NS_FAILED(rv)) {

        // If this fails, this means that the underlying array has changed and
        // we don't know it yet.  This is pretty bad and I would like to
        // figure out a nicer way to detect besides just handing the error.

        // Mark this page as not cached
        rv = SetPageCachedStatus(aIndex, eNotCached);
        NS_ENSURE_SUCCESS(rv, rv);
        mGetByIndexAsyncPending = PR_FALSE;

        NS_WARNING("Array changed out from under us - need to make this better");
        return NS_OK;
      }

      // XXXben Check to make sure that we have a nsITreeBoxObject here before
      //        modifying the selection. This was crashing occasionally, see
      //        bug 3533.
      if (mManageSelection) {
        nsCOMPtr<nsITreeBoxObject> boxObject;
        if (mRealSelection) {
          rv = mRealSelection->GetTree(getter_AddRefs(boxObject));
          NS_ENSURE_SUCCESS(rv, rv);
          NS_ASSERTION(boxObject, "No box object?! Fix bug 3533 correctly!");
        }

        // Should this row be selected?
        if (boxObject && (mSelectionIsAll || mSelectionList.Count())) {

          PRBool shouldSelect = PR_FALSE;
          if (mSelectionIsAll && !mFakeAllRow) {
            shouldSelect = PR_TRUE;
          }
          else {
            nsString id;
            rv = GetUniqueIdForIndex(index, id);
            NS_ENSURE_SUCCESS(rv, rv);
            if (mSelectionList.Get(id, nsnull)) {
              shouldSelect = PR_TRUE;
              mSelectionList.Remove(id);
            }
          }

          if (shouldSelect) {
            mSelectionChanging = PR_TRUE;
            PRInt32 row = ArrayToTree(index);
            rv = mRealSelection->RangedSelect(row, row, PR_TRUE);
            mSelectionChanging = PR_FALSE;
            NS_ENSURE_SUCCESS(rv, rv);
          }
        }
      }

      nsString* addedGuid = guids.AppendElement(guid);
      NS_ENSURE_TRUE(addedGuid, NS_ERROR_OUT_OF_MEMORY);

      const PRUnichar** addedPtr = guidArray.AppendElement(guid.get());
      NS_ENSURE_TRUE(addedPtr, NS_ERROR_OUT_OF_MEMORY);
    }

    if (mManageSelection) {
      // Unsuppress select events
      mSelectionChanging = PR_TRUE;
      rv = autoSelectEventsSuppressed.set(PR_FALSE);
      mSelectionChanging = PR_FALSE;
      NS_ENSURE_SUCCESS(rv, rv);
    }

    PRUint32 count = 0;
    sbILocalDatabaseResourcePropertyBag** bags = nsnull;
    rv = mPropertyCache->GetProperties(guidArray.Elements(), length, &count, &bags);
    NS_ENSURE_SUCCESS(rv, rv);

    // Cache each of the returned property bags
    for (PRUint32 i = 0; i < count; i++) {
      if (bags[i]) {
        PRBool success = mRowCache.Put(i + start, bags[i]);
        if (!success) {
          NS_FREE_XPCOM_ISUPPORTS_POINTER_ARRAY(count, bags);
          return NS_ERROR_OUT_OF_MEMORY;
        }

        // Now that we have the real data we can remove our temp entry from
        // mDirtyRowCache.
        mDirtyRowCache.Remove(i + start);
      }
    }

    NS_FREE_XPCOM_ISUPPORTS_POINTER_ARRAY(count, bags);

    TRACE(("sbLocalDatabaseTreeView[0x%.8x] - OnGetGuidByIndex - "
           "InvalidateRange(%d, %d)", this, start, end));

    rv = SetPageCachedStatus(aIndex, eCached);
    NS_ENSURE_SUCCESS(rv, rv);


    if (mTreeBoxObject) {
      rv = mTreeBoxObject->InvalidateRange(ArrayToTree(start),
                                           ArrayToTree(end));
      NS_ENSURE_SUCCESS(rv, rv);
    }

  }
  else {
    NS_WARNING("OnGetGuidByIndex called with error result");

    // If we get here, there was some kind of error getting the rows from the
    // array.  Mark the requested row as not cached.
    rv = SetPageCachedStatus(aIndex, eNotCached);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If there was another request on deck, send it now
  if (mNextGetByIndexAsync > -1) {
    rv = SetPageCachedStatus(mNextGetByIndexAsync, ePending);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mArray->GetGuidByIndexAsync(mNextGetByIndexAsync);
    NS_ENSURE_SUCCESS(rv, rv);
    mGetByIndexAsyncPending = PR_TRUE;
    mNextGetByIndexAsync = -1;
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnGetSortPropertyValueByIndex(PRUint32 aIndex,
                                                       const nsAString& aGUID,
                                                       nsresult aResult)
{
  NS_NOTREACHED("Shouldn't get called");
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnGetMediaItemIdByIndex(PRUint32 aIndex,
                                                 PRUint32 aMediaItemId,
                                                 nsresult aResult)
{
  NS_NOTREACHED("Shouldn't get called");
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnStateChange(PRUint32 aState)
{
  if (aState == sbILocalDatabaseAsyncGUIDArrayListener::STATE_BUSY) {
    mIsArrayBusy = PR_TRUE;
  }
  else if (aState == sbILocalDatabaseAsyncGUIDArrayListener::STATE_IDLE) {
    mIsArrayBusy = PR_FALSE;
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetPropertyForTreeColumn(nsITreeColumn* aTreeColumn,
                                                  nsAString& aProperty)
{
  NS_ENSURE_ARG_POINTER(aTreeColumn);

  nsresult rv;

  nsCOMPtr<nsIDOMElement> element;
  rv = aTreeColumn->GetElement(getter_AddRefs(element));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = element->GetAttribute(NS_LITERAL_STRING("bind"), aProperty);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetTreeColumnForProperty(const nsAString& aProperty,
                                                  nsITreeColumn** aTreeColumn)
{
  NS_ENSURE_ARG_POINTER(aTreeColumn);
  NS_ENSURE_STATE(mTreeBoxObject);

  nsCOMPtr<nsITreeColumns> columns;
  nsresult rv = mTreeBoxObject->GetColumns(getter_AddRefs(columns));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 count;
  rv = columns->GetCount(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRInt32 i = 0; i < count; i++) {
    nsCOMPtr<nsITreeColumn> column;
    rv = columns->GetColumnAt(i, getter_AddRefs(column));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDOMElement> element;
    rv = column->GetElement(getter_AddRefs(element));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString bind;
    rv = element->GetAttribute(NS_LITERAL_STRING("bind"), bind);
    NS_ENSURE_SUCCESS(rv, rv);

    if (bind.Equals(aProperty)) {
      NS_ADDREF(*aTreeColumn = column);
      return NS_OK;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

/* inline */ nsresult
sbLocalDatabaseTreeView::GetColumnPropertyInfo(nsITreeColumn* aColumn,
                                               sbIPropertyInfo** aPropertyInfo)
{
  nsString propertyID;
  nsresult rv = GetPropertyForTreeColumn(aColumn, propertyID);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPropMan->GetPropertyInfo(propertyID, aPropertyInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetPropertyInfoAndCachedValue(PRInt32 aRow,
                                                       nsITreeColumn* aColumn,
                                                       nsAString& aValue,
                                                       sbIPropertyInfo** aPropertyInfo)
{
  NS_ASSERTION(aColumn, "aColumn is null");
  NS_ASSERTION(aPropertyInfo, "aPropertyInfo is null");

  PRUint32 index = TreeToArray(aRow);
  nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
  if (!mRowCache.Get(index, getter_AddRefs(bag)) &&
      !mDirtyRowCache.Get(index, getter_AddRefs(bag))) {
    // Don't bother to do anything else if this row isn't cached yet.
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<sbIPropertyInfo> pi;
  nsresult rv = GetColumnPropertyInfo(aColumn, getter_AddRefs(pi));
  NS_ENSURE_SUCCESS(rv, rv);

  nsString propertyID;
  rv = pi->GetId(propertyID);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString value;
  rv = bag->GetProperty(propertyID, aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*aPropertyInfo = pi);
  return NS_OK;
}

nsresult
sbLocalDatabaseTreeView::GetPlayingProperty(PRUint32 aIndex,
                                            nsISupportsArray* properties)
{
  NS_ASSERTION(properties, "properties is null");

  nsresult rv;

  if (!mPlayingItemUID.IsEmpty()) {
    nsString uid;
    rv = GetUniqueIdForIndex(aIndex, uid);
    NS_ENSURE_SUCCESS(rv, rv);

    if (mPlayingItemUID.Equals(uid)) {
      rv = TokenizeProperties(NS_LITERAL_STRING("playing"), properties);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetRowCount(PRInt32 *aRowCount)
{
  NS_ENSURE_ARG_POINTER(aRowCount);

  nsresult rv;

  if (mCachedRowCountDirty) {
    if (!mCachedRowCountPending) {
      rv = mArray->GetLengthAsync();
      NS_ENSURE_SUCCESS(rv, rv);
      mCachedRowCountPending = PR_TRUE;
    }
  }

  *aRowCount = mCachedRowCount;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetCellText(PRInt32 row,
                                     nsITreeColumn *col,
                                     nsAString& _retval)
{
  NS_ENSURE_ARG_POINTER(col);

  nsresult rv;

  if (IsAllRow(row)) {
    _retval.Assign(mLocalizedAll);
    return NS_OK;
  }

  rv = GetCellPropertyValue(TreeToArray(row), col, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetSelection(nsITreeSelection** aSelection)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - GetSelection()", this));

  NS_ENSURE_ARG_POINTER(aSelection);

  if (!mSelection) {
    return NS_ERROR_FAILURE;
  }

  NS_ADDREF(*aSelection = mSelection);
  return NS_OK;
}
NS_IMETHODIMP
sbLocalDatabaseTreeView::SetSelection(nsITreeSelection* aSelection)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetSelection(0x%.8x)", this, aSelection));

  NS_ENSURE_ARG_POINTER(aSelection);

  // Wrap the selection given to us with our own proxy implementation so
  // we can get useful notifications on how the selection is changing
  if (mListType == eDistinct) {
    mSelection = new sbFilterTreeSelection(aSelection, this);
    NS_ENSURE_TRUE(mSelection, NS_ERROR_OUT_OF_MEMORY);
  }
  else {
    NS_ASSERTION(mViewSelection, "SetSelection called but no mViewSelection");
    mSelection = new sbPlaylistTreeSelection(aSelection, mViewSelection, this);
    NS_ENSURE_TRUE(mSelection, NS_ERROR_OUT_OF_MEMORY);
  }

  // Keep a ref to the real selection so we can modify it without going
  // through our proxy
  mRealSelection = aSelection;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::CycleHeader(nsITreeColumn* col)
{
  NS_ENSURE_ARG_POINTER(col);

  nsresult rv;

  // If our guid array is busy, ignore the click
  if (mIsArrayBusy) {
    return NS_OK;
  }

  nsCOMPtr<sbIMediaList> mediaList;
  rv = mMediaListView->GetMediaList(getter_AddRefs(mediaList));
  NS_ENSURE_SUCCESS(rv, rv);

  // If the list is not sortable, ignore the click
  nsAutoString isSortable;
  rv = mediaList->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_ISSORTABLE), isSortable);
  NS_ENSURE_SUCCESS(rv, rv);
  if (isSortable.Equals(NS_LITERAL_STRING("0"))) return NS_OK;

  nsString bind;
  rv = GetPropertyForTreeColumn(col, bind);
  NS_ENSURE_SUCCESS(rv, rv);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - CycleHeader %s", this,
         NS_LossyConvertUTF16toASCII(bind).get()));

  PRBool directionIsAscending = PR_TRUE;
  if (bind.Equals(mCurrentSortProperty)) {
    directionIsAscending = !mCurrentSortDirectionIsAscending;
  }

  rv = SetSort(bind, directionIsAscending);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mObserver) {
    nsCOMPtr<sbIMediaListViewTreeViewObserver> observer =
      do_QueryReferent(mObserver);
    if (observer) {
      rv = observer->CycleHeader(col);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::CycleCell(PRInt32 row,
                                   nsITreeColumn* col)
{
  NS_ENSURE_ARG_POINTER(col);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetRowProperties(PRInt32 row,
                                          nsISupportsArray* properties)
{
  NS_ENSURE_ARG_MIN(row, 0);
  NS_ENSURE_ARG_POINTER(properties);

  nsresult rv;

  // If we are not managing our selection, sync up the selected state of this
  // row with the view's selection
  if (!mCachedRowCountDirty && !mManageSelection &&
      mRealSelection && mViewSelection) {
    PRBool viewIsSelected;
    rv = mViewSelection->IsIndexSelected(row, &viewIsSelected);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool treeIsSelected;
    rv = mRealSelection->IsSelected(row, &treeIsSelected);
    NS_ENSURE_SUCCESS(rv, rv);

    if (viewIsSelected != treeIsSelected) {
      rv = mRealSelection->ToggleSelect(row);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  PRUint32 count;
  properties->Count(&count);
  nsString props;
  for (PRUint32 i = 0; i < count; i++) {
    nsCOMPtr<nsIAtom> atom;
    properties->QueryElementAt(i, NS_GET_IID(nsIAtom), getter_AddRefs(atom));
    if (atom) {
      nsString s;
      atom->ToString(s);
      props.Append(s);
      props.AppendLiteral(" ");
    }
  }

  if (IsAllRow(row)) {
    rv = TokenizeProperties(NS_LITERAL_STRING("all"), properties);
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  PRUint32 index = TreeToArray(row);

  rv = GetPlayingProperty(index, properties);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILocalDatabaseResourcePropertyBag> bag;
  if (!mRowCache.Get(index, getter_AddRefs(bag)) &&
      !mDirtyRowCache.Get(index, getter_AddRefs(bag))) {
    // Don't bother to do anything else if this row isn't cached yet.
    return NS_OK;
  }

  nsCOMPtr<nsIStringEnumerator> propertyEnumerator;
  rv = mPropMan->GetPropertyIDs(getter_AddRefs(propertyEnumerator));
  NS_ENSURE_SUCCESS(rv, rv);

  nsString propertyID;
  while (NS_SUCCEEDED(propertyEnumerator->GetNext(propertyID))) {

    nsAutoString value;
    nsresult rv = bag->GetProperty(propertyID, value);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbIPropertyInfo> propInfo;
    rv = mPropMan->GetPropertyInfo(propertyID, getter_AddRefs(propInfo));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbITreeViewPropertyInfo> tvpi = do_QueryInterface(propInfo, &rv);
    if (NS_SUCCEEDED(rv)) {
      nsString propertiesString;
      rv = tvpi->GetRowProperties(value, propertiesString);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!propertiesString.IsEmpty()) {
        rv = TokenizeProperties(propertiesString, properties);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetCellProperties(PRInt32 row,
                                           nsITreeColumn* col,
                                           nsISupportsArray* properties)
{
  NS_ENSURE_ARG_MIN(row, 0);
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_ARG_POINTER(properties);

#ifdef PR_LOGGING
  PRInt32 colIndex = -1;
  col->GetIndex(&colIndex);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - GetCellProperties(%d, %d)", this,
         row, colIndex));
#endif

  if (IsAllRow(row)) {
    return NS_OK;
  }

  nsresult rv;

  rv = GetColumnProperties(col, properties);
  NS_ENSURE_SUCCESS(rv, rv);

  // Add the mouse states
  if (mMouseStateRow == row && mMouseStateColumn == col) {
    switch(mMouseState) {
      case sbILocalDatabaseTreeView::MOUSE_STATE_HOVER:
        rv = TokenizeProperties(NS_LITERAL_STRING("cell-hover"), properties);
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      case sbILocalDatabaseTreeView::MOUSE_STATE_DOWN:
        rv = TokenizeProperties(NS_LITERAL_STRING("cell-active"), properties);
        NS_ENSURE_SUCCESS(rv, rv);
        break;
    }
  }

  rv = GetPlayingProperty(TreeToArray(row), properties);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIPropertyInfo> pi;
  nsString value;
  rv = GetPropertyInfoAndCachedValue(row, col, value, getter_AddRefs(pi));
  // Returns NS_ERROR_NOT_AVAILABLE when the row is not cached
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbITreeViewPropertyInfo> tvpi = do_QueryInterface(pi, &rv);
  if (NS_SUCCEEDED(rv)) {
    nsString propertiesString;
    rv = tvpi->GetCellProperties(value, propertiesString);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!propertiesString.IsEmpty()) {
      rv = TokenizeProperties(propertiesString, properties);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  nsCOMPtr<sbIClickablePropertyInfo> cpi = do_QueryInterface(pi, &rv);
  if (NS_SUCCEEDED(rv)) {
    PRBool isDisabled;
    rv = cpi->IsDisabled(value, &isDisabled);
    NS_ENSURE_SUCCESS(rv, rv);

    if (isDisabled) {
      rv = TokenizeProperties(NS_LITERAL_STRING("disabled"), properties);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetColumnProperties(nsITreeColumn* col,
                                             nsISupportsArray* properties)
{
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_ARG_POINTER(properties);

  nsAutoString propertyID;
  nsresult rv = GetPropertyForTreeColumn(col, propertyID);
  NS_ENSURE_SUCCESS(rv, rv);

  // Turn the property name into something that CSS can handle.  For example
  //
  //   http://songbirdnest.com/data/1.0#rating
  //
  // becomes:
  //
  //   http-songbirdnest-com-data-1-0-rating

  NS_NAMED_LITERAL_STRING(badChars, BAD_CSS_CHARS);
  static const PRUnichar kHyphenChar = '-';

  for (PRUint32 index = 0; index < propertyID.Length(); index++) {
    PRUnichar testChar = propertyID.CharAt(index);

    // Short circuit for ASCII alphanumerics.
    if ((testChar >= 97 && testChar <= 122) || // a-z
        (testChar >= 65 && testChar <= 90) ||  // A-Z
        (testChar >= 48 && testChar <= 57)) {  // 0-9
      continue;
    }

    PRInt32 badCharIndex= badChars.FindChar(testChar);
    if (badCharIndex > -1) {
      if (index > 0 && propertyID.CharAt(index - 1) == kHyphenChar) {
        propertyID.Replace(index, 1, nsnull, 0);
        index--;
      }
      else {
        propertyID.Replace(index, 1, kHyphenChar);
      }
    }
  }

  rv = TokenizeProperties(propertyID, properties);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsContainer(PRInt32 row, PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = PR_FALSE;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsContainerOpen(PRInt32 row, PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = PR_FALSE;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsContainerEmpty(PRInt32 row, PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsSeparator(PRInt32 row, PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = PR_FALSE;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsSorted(PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  // Media lists are always sorted
  *_retval = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::CanDrop(PRInt32 row,
                                 PRInt32 orientation,
                                 PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - CanDrop(%d, %d)", this,
         row, orientation));

  if (!IsAllRow(row) && mObserver) {

    nsCOMPtr<sbIMediaListViewTreeViewObserver> observer =
      do_QueryReferent(mObserver);
    if (observer) {
      nsresult rv = observer->CanDrop(TreeToArray(row),
                                      orientation,
                                      _retval);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  else {
    *_retval = PR_FALSE;
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::Drop(PRInt32 row, PRInt32 orientation)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - Drop(%d, %d)", this,
         row, orientation));

  if (!IsAllRow(row) && mObserver) {

    nsCOMPtr<sbIMediaListViewTreeViewObserver> observer =
      do_QueryReferent(mObserver);
    if (observer) {
      nsresult rv = observer->Drop(TreeToArray(row), orientation);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetParentIndex(PRInt32 rowIndex, PRInt32* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::HasNextSibling(PRInt32 rowIndex,
                                        PRInt32 afterIndex,
                                        PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetLevel(PRInt32 row, PRInt32* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = 0;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetImageSrc(PRInt32 row,
                                     nsITreeColumn* col,
                                     nsAString& _retval)
{
  NS_ENSURE_ARG_POINTER(col);

  if (IsAllRow(row)) {
    return NS_OK;
  }

  nsresult rv;

  nsString value;
  nsCOMPtr<sbIPropertyInfo> pi;
  rv = GetPropertyInfoAndCachedValue(row, col, value, getter_AddRefs(pi));
  // Returns NS_ERROR_NOT_AVAILABLE when the row is not cached
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbITreeViewPropertyInfo> tvpi = do_QueryInterface(pi, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = tvpi->GetImageSrc(value, _retval);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetProgressMode(PRInt32 row,
                                         nsITreeColumn* col,
                                         PRInt32* _retval)
{
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_ARG_POINTER(_retval);

  if (IsAllRow(row)) {
    *_retval = nsITreeView::PROGRESS_NONE;
    return NS_OK;
  }

  nsresult rv;

  nsString value;
  nsCOMPtr<sbIPropertyInfo> pi;
  rv = GetPropertyInfoAndCachedValue(row, col, value, getter_AddRefs(pi));
  // Returns NS_ERROR_NOT_AVAILABLE when the row is not cached
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbITreeViewPropertyInfo> tvpi = do_QueryInterface(pi, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = tvpi->GetProgressMode(value, _retval);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetCellValue(PRInt32 row,
                                      nsITreeColumn* col,
                                      nsAString& _retval)
{
#ifdef PR_LOGGING
  PRInt32 colIndex = -1;
  col->GetIndex(&colIndex);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - GetCellValue(%d, %d)", this,
         row, colIndex));
#endif

  if (IsAllRow(row)) {
    _retval.Truncate();
    return NS_OK;
  }

  nsresult rv;

  nsCOMPtr<sbIPropertyInfo> pi;
  nsString value;
  rv = GetPropertyInfoAndCachedValue(row, col, value, getter_AddRefs(pi));
  // Returns NS_ERROR_NOT_AVAILABLE when the row is not cached
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbITreeViewPropertyInfo> tvpi = do_QueryInterface(pi, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = tvpi->GetCellValue(value, _retval);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::SetTree(nsITreeBoxObject *tree)
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetTree(0x%.8x)", this, tree));
  nsresult rv;

  // XXX: nsTreeBoxObject calls this method with a null to break a cycle so
  // we can't NS_ENSURE_ARG_POINTER(tree)

  mTreeBoxObject = tree;

  if (tree) {
    nsresult rv = UpdateColumnSortAttributes(mCurrentSortProperty,
                                             mCurrentSortDirectionIsAscending);
    NS_ENSURE_SUCCESS(rv, rv);

    // Rebuild view with the new tree
    rv = Rebuild();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Manage our listener to the playback service.  Attach the listener when
  // we are bound to a tree and remove it when we are unbound.  We do this
  // because we need to remove our listener when we go away, but we can't do
  // it in the destructor because we wind up creating a wrapper that eventually
  // points to a deleted this.
  if (mPlaylistPlayback) {
    nsCOMPtr<sbIPlaylistPlaybackListener> playbackListener =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbIPlaylistPlaybackListener*, this), &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (tree) {
      if (!mIsListeningToPlayback) {
        rv = mPlaylistPlayback->AddListener(playbackListener);
        NS_ENSURE_SUCCESS(rv, rv);
        mIsListeningToPlayback = PR_TRUE;
        
        PRBool isPlaying;
        rv = mPlaylistPlayback->GetPlaying(&isPlaying);
        NS_ENSURE_SUCCESS(rv, rv);

        // If we are already playing, initialize the playing indicator
        if (isPlaying) {
          nsCOMPtr<sbIMediaListView> playingView;
          rv = mPlaylistPlayback->GetPlayingView(getter_AddRefs(playingView));
          NS_ENSURE_SUCCESS(rv, rv);

          PRInt32 playingIndex;
          rv = mPlaylistPlayback->GetCurrentIndex(&playingIndex);
          NS_ENSURE_SUCCESS(rv, rv);

          if (playingIndex >= 0) {
            nsCOMPtr<sbIMediaItem> playingItem;
            rv = playingView->GetItemByIndex(playingIndex,
                                             getter_AddRefs(playingItem));
            NS_ENSURE_SUCCESS(rv, rv);

            // XXXsteve It is possible that the item at the currently playing index
            // is not actually the playing item if the item has been removed from
            // the view.  Double check this before lighting up the indicator.
            // This should be removed when bug 7409 is fixed.
            nsString playingItemGUID;
            rv = playingItem->GetGuid(playingItemGUID);
            NS_ENSURE_SUCCESS(rv, rv);

            nsString actuallyPlayingGUID;
            rv = mPlaylistPlayback->GetCurrentGUID(actuallyPlayingGUID);
            NS_ENSURE_SUCCESS(rv, rv);

            if (playingItemGUID.Equals(actuallyPlayingGUID)) {
              rv = OnTrackChange(playingItem, playingView, playingIndex);
              NS_ENSURE_SUCCESS(rv, rv);
            }
          }
        }
      }
      else {
        NS_WARNING("Tree set but we're already listening to playback!");
      }
    }
    else {
      if (mIsListeningToPlayback) {
        rv = mPlaylistPlayback->RemoveListener(playbackListener);
        NS_ENSURE_SUCCESS(rv, rv);
        mIsListeningToPlayback = PR_FALSE;
      }
      else {
        NS_WARNING("Tree nulled but we're not listening to playback!");
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::ToggleOpenState(PRInt32 row)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::SelectionChanged()
{
  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SelectionChanged()", this));

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsEditable(PRInt32 row,
                                    nsITreeColumn* col,
                                    PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_ARG_POINTER(_retval);

  if (IsAllRow(row)) {
    *_retval = PR_FALSE;
    return NS_OK;
  }

  nsresult rv;

  nsCOMPtr<sbIPropertyInfo> propInfo;
  nsAutoString bind;
  rv = GetColumnPropertyInfo(col, getter_AddRefs(propInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = propInfo->GetUserEditable(_retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::IsSelectable(PRInt32 row,
                                      nsITreeColumn* col,
                                      PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_ARG_POINTER(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::SetCellValue(PRInt32 row,
                                      nsITreeColumn* col,
                                      const nsAString& value)
{
  NS_ENSURE_ARG_POINTER(col);

  nsresult rv;
#ifdef PR_LOGGING
  PRInt32 colIndex;
  rv = col->GetIndex(&colIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetCellValue(%d, %d %s)", this,
         row, colIndex, NS_LossyConvertUTF16toASCII(value).get()));
#endif
  rv = SetCellText(row, col, value);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::SetCellText(PRInt32 row,
                                     nsITreeColumn* col,
                                     const nsAString& value)
{
  NS_ENSURE_ARG_POINTER(col);
  NS_ENSURE_STATE(mMediaListView);

  if (IsAllRow(row)) {
    return NS_OK;
  }

  nsresult rv;
#ifdef PR_LOGGING
  PRInt32 colIndex;
  rv = col->GetIndex(&colIndex);
  NS_ENSURE_SUCCESS(rv, rv);

  TRACE(("sbLocalDatabaseTreeView[0x%.8x] - SetCellText(%d, %d %s)", this,
         row, colIndex, NS_LossyConvertUTF16toASCII(value).get()));
#endif

  nsCOMPtr<sbIPropertyInfo> info;
  rv = GetColumnPropertyInfo(col, getter_AddRefs(info));
  NS_ENSURE_SUCCESS(rv, rv);

  // Only allow |sbIClickablePropertyInfo|s to be set to null
  nsCOMPtr<sbIClickablePropertyInfo> clickable = do_QueryInterface(info, &rv);
  if (NS_FAILED(rv) && (value.IsEmpty() || value.EqualsLiteral(" "))) {
    return NS_OK;
  }

  nsAutoString bind;
  rv = GetPropertyForTreeColumn(col, bind);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString guid;
  rv = mArray->GetGuidByIndex(TreeToArray(row), guid);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaList> mediaList;
  rv = mMediaListView->GetMediaList(getter_AddRefs(mediaList));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILibrary> library;
  rv = mediaList->GetLibrary(getter_AddRefs(library));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item;
  rv = library->GetMediaItem(guid, getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString oldValue;
  rv = item->GetProperty(bind, oldValue);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!value.Equals(oldValue)) {
    rv = item->SetProperty(bind, value);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::PerformAction(const PRUnichar* action)
{
  NS_ENSURE_ARG_POINTER(action);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::PerformActionOnRow(const PRUnichar* action,
                                            PRInt32 row)
{
  NS_ENSURE_ARG_POINTER(action);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::PerformActionOnCell(const PRUnichar* action,
                                             PRInt32 row,
                                             nsITreeColumn* col)
{
  NS_ENSURE_ARG_POINTER(action);
  NS_ENSURE_ARG_POINTER(col);

  return NS_ERROR_NOT_IMPLEMENTED;
}

/**
 * See sbIMediaListViewTreeView
 */
NS_IMETHODIMP
sbLocalDatabaseTreeView::GetNextRowIndexForKeyNavigation(const nsAString& aKeyString,
                                                         PRUint32 aStartFrom,
                                                         PRInt32* _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "wrong thread");
  NS_ENSURE_FALSE(aKeyString.IsEmpty(), NS_ERROR_INVALID_ARG);
  NS_ENSURE_ARG_POINTER(_retval);

  // If we don't know how many rows should be in this table then bail.
  // Otherwise the algorithm below can loop forever.
  if (mCachedRowCountDirty) {
    NS_WARNING("We don't know how many rows are available. Bailing.");
    *_retval = -1;
    return NS_OK;
  }

  nsAutoString keyString(aKeyString);
  PRUint32 keyStringLength = keyString.Length();

  // Make sure that this is lowercased. The obj_sortable table should always be
  // lowercased.
  ToLowerCase(keyString);

  // Most folks will use this function to check the very next row in the tree.
  // Try that before doing the cache search or the costly database search.
  nsAutoString testString;
  nsresult rv = mArray->GetSortPropertyValueByIndex(aStartFrom, testString);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!keyString.Compare(Substring(testString, 0, keyStringLength))) {
    *_retval = aStartFrom;
    return NS_OK;
  }

  // Now search the cache. This will be way faster than the database search.
  PRInt32 comparison, lastRow;
  PRUint32 rowsSeen, index;

  // Three conditions to bail:
  //   1. The test string is no longer larger than the key string.
  //   2. We have iterated past the maximum number of rows in the list.
  //   3. We have passed all the rows currently cached.
  for (comparison = 1, lastRow = -1, rowsSeen = 0, index = aStartFrom + 1;
       comparison == 1 && index < mCachedRowCount && rowsSeen < mRowCache.Count();
       index++) {

    if (!mRowCache.Get(index, nsnull)) {
      // We haven't cached this row yet so continue.
      continue;
    }

    // Get the sort property value for the row.
    rv = mArray->GetSortPropertyValueByIndex(index, testString);
    NS_ENSURE_SUCCESS(rv, rv);

    // See how it compares to our key string.
    comparison = keyString.Compare(Substring(testString, 0, keyStringLength));

    if (comparison != 0) {
      // The string didn't match, but remember that this row was cached.
      lastRow = index;
      rowsSeen++;

      // And keep churning.
      continue;
    }

    // We know that we've found the right spot if the row immediately
    // preceeding this one was cached yet did not match or if this is the
    // first row to be searched.
    if ((lastRow == ((PRInt32) index) - 1) || (index == aStartFrom)) {
      *_retval = index;
      return NS_OK;
    }
  }

  // We didn't find a sure spot in the cache, so ask the database for the real
  // answer.
  rv = mArray->GetFirstIndexByPrefix(keyString, &index);
  if (NS_FAILED(rv) || (index < aStartFrom)) {
    *_retval = -1;
    return NS_OK;
  }

  // We're going to assume that the caller wants to look at the row we're about
  // to return. Trick the GUID array into caching the page that it's on so that
  // the data will arrive faster.
  rv = mArray->GetGuidByIndexAsync(index);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "GetGuidByIndexAsync");

  *_retval = (PRInt32)index;
  return NS_OK;
}

/**
 * See sbIMediaListViewTreeView
 */
NS_IMETHODIMP
sbLocalDatabaseTreeView::SetObserver(sbIMediaListViewTreeViewObserver* aObserver)
{
  nsresult rv;

  if (aObserver) {
    mObserver = do_GetWeakReference(aObserver, &rv);;
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    mObserver = nsnull;
  }

  return NS_OK;
}

/**
 * See sbIMediaListViewTreeView
 */
NS_IMETHODIMP
sbLocalDatabaseTreeView::GetObserver(sbIMediaListViewTreeViewObserver** aObserver)
{
  NS_ENSURE_ARG_POINTER(aObserver);

  *aObserver = nsnull;
  if (mObserver) {
    nsCOMPtr<sbIMediaListViewTreeViewObserver> observer =
      do_QueryReferent(mObserver);
    if (observer) {
      NS_ADDREF(*aObserver = observer);
    }
  }

  return NS_OK;
}

// sbIPlaylistPlaybackListener
NS_IMETHODIMP
sbLocalDatabaseTreeView::OnStop()
{
  nsresult rv;

  mPlayingItemUID = EmptyString();

  if (mTreeBoxObject) {
    rv = mTreeBoxObject->Invalidate();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnBeforeTrackChange(sbIMediaItem* aItem,
                                             sbIMediaListView* aView,
                                             PRUint32 aIndex)
{
  NS_ENSURE_ARG_POINTER(aItem);
  NS_ENSURE_ARG_POINTER(aView);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnTrackChange(sbIMediaItem* aItem,
                                       sbIMediaListView* aView,
                                       PRUint32 aIndex)
{
  NS_ENSURE_ARG_POINTER(aItem);
  NS_ENSURE_ARG_POINTER(aView);

  nsresult rv;

  if (mMediaListView) {
    nsCOMPtr<sbIMediaList> viewList;
    rv = mMediaListView->GetMediaList(getter_AddRefs(viewList));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbIMediaList> playingList;
    rv = aView->GetMediaList(getter_AddRefs(playingList));
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool equals;
    rv = viewList->Equals(playingList, &equals);
    NS_ENSURE_SUCCESS(rv, rv);

    if (equals) {
      nsString uid;
      rv = aView->GetViewItemUIDForIndex(aIndex, uid);
      NS_ENSURE_SUCCESS(rv, rv);

      PRUint32 index;
      rv = mMediaListView->GetIndexForViewItemUID(uid, &index);
      if (NS_SUCCEEDED(rv)) {
        rv = GetUniqueIdForIndex(index, mPlayingItemUID);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        mPlayingItemUID = EmptyString();
      }
    }
    else {
      mPlayingItemUID = EmptyString();
    }
  }

  if (mTreeBoxObject) {
    rv = mTreeBoxObject->Invalidate();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// sbIMediaListViewSelectionListener
NS_IMETHODIMP
sbLocalDatabaseTreeView::OnSelectionChanged()
{
  LOG(("sbLocalDatabaseTreeView[0x%.8x] - OnSelectionChanged", this));

  if (mListType != eDistinct && mTreeBoxObject && mRealSelection) {
    nsresult rv = mTreeBoxObject->Invalidate();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::OnCurrentIndexChanged()
{
  nsresult rv;
  PRInt32 currentIndex;

  if (mRealSelection && mViewSelection) {
    rv = mViewSelection->GetCurrentIndex(&currentIndex);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mRealSelection->SetCurrentIndex(currentIndex);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// nsIClassInfo
NS_IMETHODIMP
sbLocalDatabaseTreeView::GetInterfaces(PRUint32* count, nsIID*** array)
{
  return NS_CI_INTERFACE_GETTER_NAME(sbLocalDatabaseTreeView)(count, array);
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetHelperForLanguage(PRUint32 language,
                                              nsISupports** _retval)
{
  *_retval = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetContractID(char** aContractID)
{
  *aContractID = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetClassDescription(char** aClassDescription)
{
  *aClassDescription = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetClassID(nsCID** aClassID)
{
  *aClassID = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetImplementationLanguage(PRUint32* aImplementationLanguage)
{
  *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetFlags(PRUint32 *aFlags)
{
  *aFlags = 0;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeView::GetClassIDNoAlloc(nsCID* aClassIDNoAlloc)
{
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMPL_ISUPPORTS1(sbLocalDatabaseTreeViewState,
                   nsISerializable);

sbLocalDatabaseTreeViewState::sbLocalDatabaseTreeViewState() :
  mSelectionIsAll(PR_FALSE)
{
}

NS_IMETHODIMP
sbLocalDatabaseTreeViewState::Read(nsIObjectInputStream* aStream)
{
  NS_ENSURE_ARG_POINTER(aStream);

  nsresult rv;

  rv = aStream->ReadObject(PR_TRUE, getter_AddRefs(mSort));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 selectionCount;
  rv = aStream->Read32(&selectionCount);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < selectionCount; i++) {
    nsString key;
    nsString entry;

    rv = aStream->ReadString(key);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aStream->ReadString(entry);
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool success = mSelectionList.Put(key, entry);
    NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);
  }

  PRBool selectionIsAll;
  rv = aStream->ReadBoolean(&selectionIsAll);
  NS_ENSURE_SUCCESS(rv, rv);
  mSelectionIsAll = selectionIsAll;

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseTreeViewState::Write(nsIObjectOutputStream* aStream)
{
  NS_ENSURE_ARG_POINTER(aStream);

  nsresult rv;

  rv = aStream->WriteObject(mSort, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aStream->Write32(mSelectionList.Count());
  NS_ENSURE_SUCCESS(rv, rv);

  mSelectionList.EnumerateRead(SB_SerializeSelectionListCallback, aStream);

  rv = aStream->WriteBoolean(mSelectionIsAll);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeViewState::Init()
{
  PRBool success = mSelectionList.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  return NS_OK;
}

nsresult
sbLocalDatabaseTreeViewState::ToString(nsAString& aStr)
{
  nsresult rv;
  nsString buff;
  nsString temp;

  rv = mSort->ToString(temp);
  NS_ENSURE_SUCCESS(rv, rv);

  buff.Assign(temp);

  buff.AppendLiteral(" selection ");
  if (mSelectionIsAll) {
    buff.AppendLiteral("is all");
  }
  else {
    buff.AppendInt(mSelectionList.Count());
    buff.AppendLiteral(" items");
  }

  aStr = buff;

  return NS_OK;
}
