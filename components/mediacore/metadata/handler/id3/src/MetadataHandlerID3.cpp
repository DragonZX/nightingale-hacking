/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright 2006 Pioneers of the Inevitable LLC
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the GPL).
// 
// Software distributed under the License is distributed 
// on an AS IS basis, WITHOUT WARRANTY OF ANY KIND, either 
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

/**
* \file MetadataHandlerID3.cpp
* \brief 
*/

#pragma once

// INCLUDES ===================================================================
#ifdef XP_WIN
#include <windows.h> // For ansi-code-page character translation.
#endif

#include <nscore.h>
#include "MetadataHandlerID3.h"

#include <necko/nsIURI.h>
#include <necko/nsIFileStreams.h>
#include <necko/nsIIOService.h>
#include <necko/nsNetUtil.h>

#include <string/nsReadableUtils.h>
#include <xpcom/nsEscape.h>

// DEFINES ====================================================================

#include <id3/reader.h>
  
class MetadataHandlerID3Exception
{
private:
  MetadataHandlerID3Exception() {}
public:
  MetadataHandlerID3Exception( PRUint64 seek, PRUint64 buf, PRUint64 size ) : m_Seek( seek ), m_Buf( buf ), m_Size( size ) {}
  PRUint64 m_Seek, m_Buf, m_Size;
};


class ID3_ChannelReader : public ID3_Reader
{
  nsCOMPtr<sbIMetadataChannel> m_Channel;
protected:
public:
  ID3_ChannelReader()
  {
  }
  ID3_ChannelReader(sbIMetadataChannel* channel)
  {
    setChannel( channel );
  };
  virtual ~ID3_ChannelReader() { ; }
  virtual void close() { ; }

  void setChannel(sbIMetadataChannel* channel)
  {
    m_Channel = channel;
    this->setCur( 0 );  // Open it as a restartable channel
  }

  virtual int_type peekChar() 
  { 
    if (!this->atEnd() || !this->getCur())
    {
      char aByte;
      PRUint32 count = 0;
      m_Channel->Read( &aByte, 1, &count ); // 1 byte, please.
      this->setCur( this->getCur() - 1 ); // Now back up 1 byte.  Streams shouldn't peek!
      return aByte;
    }
    return END_OF_READER;
  }

  virtual size_type readChars(char buf[], size_type len)
  {
    PRUint32 count = 0;
    if ( ! NS_SUCCEEDED( m_Channel->Read( buf, len, &count ) ) )
    {
      PRUint64 pos, buf, size;
      m_Channel->GetPos( &pos );
      m_Channel->GetBuf( &buf );
      m_Channel->GetSize( &size );
      throw MetadataHandlerID3Exception( pos + len, buf, size );
    }
    return count;
  }
  virtual size_type readChars(char_type buf[], size_type len)
  {
    return this->readChars(reinterpret_cast<char *>(buf), len);
  }

  virtual pos_type getCur() 
  { 
    PRUint64 pos = 0;
    m_Channel->GetPos( &pos );
    return (pos_type)pos;
  }

  virtual pos_type getBeg()
  {
    return 0;
  }

  virtual pos_type getEnd()
  {
    PRUint64 size = 0;
    m_Channel->GetSize( &size );
    return (pos_type)size;
  }

  virtual pos_type remainingBytes()
  {
    return getEnd() - getCur();
  }

  virtual pos_type skipChars(pos_type skip)
  {
    return setCur( getCur() + skip );
  }

  virtual pos_type setCur(pos_type pos)
  {
    if ( !NS_SUCCEEDED( m_Channel->SetPos( pos ) ) )
    {
      PRUint64 seek, buf, size;
      m_Channel->GetPos( &seek );
      m_Channel->GetBuf( &buf );
      m_Channel->GetSize( &size );
      throw MetadataHandlerID3Exception( seek, buf, size );
    }
    return pos;
  }
};

// FUNCTIONS ==================================================================

// CLASSES ====================================================================
NS_IMPL_ISUPPORTS1(sbMetadataHandlerID3, sbIMetadataHandler)

//-----------------------------------------------------------------------------
sbMetadataHandlerID3::sbMetadataHandlerID3()
{
  m_Completed = false;
} //ctor

//-----------------------------------------------------------------------------
sbMetadataHandlerID3::~sbMetadataHandlerID3()
{
  m_Completed = true;
} //dtor

//-----------------------------------------------------------------------------
/* void SupportedMIMETypes (out PRUint32 nMIMECount, [array, size_is (nMIMECount), retval] out wstring aMIMETypes); */
NS_IMETHODIMP sbMetadataHandlerID3::SupportedMIMETypes(PRUint32 *nMIMECount, PRUnichar ***aMIMETypes)
{
  return NS_ERROR_NOT_IMPLEMENTED;
} //SupportedMIMETypes

//-----------------------------------------------------------------------------
/* void SupportedFileExtensions (out PRUint32 nExtCount, [array, size_is (nExtCount), retval] out wstring aExts); */
NS_IMETHODIMP sbMetadataHandlerID3::SupportedFileExtensions(PRUint32 *nExtCount, PRUnichar ***aExts)
{
  return NS_ERROR_NOT_IMPLEMENTED;
} //SupportedFileExtensions

//-----------------------------------------------------------------------------
/* nsIChannel GetChannel (); */
NS_IMETHODIMP sbMetadataHandlerID3::GetChannel(nsIChannel **_retval)
{
  *_retval = m_Channel.get();
  (*_retval)->AddRef();

  return NS_OK;
} //GetChannel

//-----------------------------------------------------------------------------
/* PRInt32 SetChannel (in nsIChannel urlChannel); */
NS_IMETHODIMP sbMetadataHandlerID3::SetChannel(nsIChannel *urlChannel, PRInt32 *_retval)
{
  *_retval = 0;
  m_Channel = urlChannel;

  return NS_OK;
} //SetChannel

NS_IMETHODIMP sbMetadataHandlerID3::OnChannelData( nsISupports *channel )
{
  nsCOMPtr<sbIMetadataChannel> mc( do_QueryInterface(channel) ) ;

  if ( mc.get() )
  {
    try
    {
      if ( !m_Completed )
      {
        ID3_Tag  tag;
        ID3_ChannelReader channel_reader( mc.get() );
        bool ok = tag.Parse( channel_reader );
        ReadTag(tag);
/*
        // How do I calculate length without bitrate?
        PRUnichar *bitrate;
        m_Values->GetValue( NS_LITERAL_STRING("bitrate").get(), &bitrate );
        if ( bitrate )
        {
        }
*/
      }
    }
    catch ( const MetadataHandlerID3Exception err )
    {
      PRBool completed = false;
      mc->Completed( &completed );
      // If it's a tiny file, it's probably a 404 error
      if ( completed || ( err.m_Seek > ( err.m_Size - 1024 ) ) )
      {
        // If it's a big file, this means it's an ID3v1 and it needs to seek to the end of the track?  Ooops.
        m_Completed = true;
      }

      // Otherwise, take another spin around and try again.
    }
  }

  return NS_OK;
}

NS_IMETHODIMP sbMetadataHandlerID3::Completed(PRBool *_retval)
{
  *_retval = m_Completed;

  return NS_OK;
} //SetChannel

NS_IMETHODIMP sbMetadataHandlerID3::Close()
{
  if ( m_ChannelHandler.get() ) 
    m_ChannelHandler->Close();

  m_Values = nsnull;
  m_Channel = nsnull;
  m_ChannelHandler = nsnull;

  return NS_OK;
} //Close

NS_IMETHODIMP sbMetadataHandlerID3::Vote(const PRUnichar *url, PRInt32 *_retval )
{
  nsString strUrl( url );

  if ( strUrl.Find( ".mp3", PR_TRUE ) != -1 )
    *_retval = 1;
  else
    *_retval = 0;

  return NS_OK;
} //Close

//-----------------------------------------------------------------------------
/* PRInt32 Read (); */
NS_IMETHODIMP sbMetadataHandlerID3::Read(PRInt32 *_retval)
{
  nsresult nRet = NS_ERROR_UNEXPECTED;

  if ( m_Completed )
  {
    *_retval = -1;
    return NS_ERROR_UNEXPECTED;
  }

  *_retval = 0;
  if(!m_Channel)
  {
    *_retval = -1;
    return NS_ERROR_FAILURE;
  }

  // Get a new values object.
  m_Values = do_CreateInstance("@songbird.org/Songbird/MetadataValues;1");
  if(!m_Values.get())
  {
    *_retval = -1;
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIURI> pURI;
  nRet = m_Channel->GetURI(getter_AddRefs(pURI));
  if(NS_FAILED(nRet)) return nRet;

  PRBool async = true;

  nsCString cstrScheme, cstrPath;
  pURI->GetScheme(cstrScheme);
  pURI->GetPath(cstrPath);
  if(cstrScheme.Equals(NS_LITERAL_CSTRING("file")))
  {
#if defined(XP_WIN)
    nsCString::iterator itBegin, itEnd;

    if(StringBeginsWith(cstrPath, NS_LITERAL_CSTRING("/")))
      cstrPath.Cut(0, 1);

    cstrPath.BeginWriting(itBegin);
    cstrPath.EndWriting(itEnd);

    while(itBegin != itEnd)
    {
      if( (*itBegin) == '/') (*itBegin) = '\\';
      itBegin++;
    }

    if ( cstrPath.Find( NS_LITERAL_CSTRING(":\\") ) == 1 )
    {
      async = false;
    }
#endif
  }

  if ( !async )
  {
    // Right now, only the fast Windows code uses the synchronous reader on local files.
    ID3_Tag  tag;
    size_t nTagSize = tag.Link(NS_UnescapeURL(cstrPath).get());
    *_retval = nTagSize;

    ReadTag(tag);

    nRet = NS_OK;
  }
  else
  {
    // Get a new channel handler for async operation.
    m_ChannelHandler = do_CreateInstance("@songbird.org/Songbird/MetadataChannel;1");
    if(!m_ChannelHandler.get())
    {
      *_retval = -1;
      return NS_ERROR_FAILURE;
    }
    m_ChannelHandler->Open( m_Channel, this );

    nRet = NS_OK;
  }

  return nRet;
} //Read

//-----------------------------------------------------------------------------
/* PRInt32 Write (); */
NS_IMETHODIMP sbMetadataHandlerID3::Write(PRInt32 *_retval)
{
  *_retval = 0;
  return NS_OK;
} //Write

/* sbIMetadataValues GetValuesMap (); */
NS_IMETHODIMP sbMetadataHandlerID3::GetValuesMap(sbIMetadataValues **_retval)
{
  if ( ! m_Completed )
    return NS_ERROR_UNEXPECTED;
  *_retval = m_Values;
  if ( (*_retval) )
    (*_retval)->AddRef();
  return NS_OK;
}

/* void SetValuesMap (in sbIMetadataValues values); */
NS_IMETHODIMP sbMetadataHandlerID3::SetValuesMap(sbIMetadataValues *values)
{
  m_Values = values;
  return NS_ERROR_NOT_IMPLEMENTED;
}

//-----------------------------------------------------------------------------
/* PRInt32 GetNumAvailableTags (); */
NS_IMETHODIMP sbMetadataHandlerID3::GetNumAvailableTags(PRInt32 *_retval)
{
  *_retval = 0;
  return NS_OK;
} //GetNumAvailableTags

//-----------------------------------------------------------------------------
/* void GetAvailableTags (out PRUint32 tagCount, [array, size_is (tagCount), retval] out wstring tags); */
NS_IMETHODIMP sbMetadataHandlerID3::GetAvailableTags(PRUint32 *tagCount, PRUnichar ***tags)
{
  return NS_OK;
} //GetAvailableTags

//-----------------------------------------------------------------------------
/* wstring GetTag (in wstring tagName); */
NS_IMETHODIMP sbMetadataHandlerID3::GetTag(const PRUnichar *tagName, PRUnichar **_retval)
{

  return NS_OK;
} //GetTag

//-----------------------------------------------------------------------------
/* PRInt32 SetTag (in wstring tagName, in wstring tagValue); */
NS_IMETHODIMP sbMetadataHandlerID3::SetTag(const PRUnichar *tagName, const PRUnichar *tagValue, PRInt32 *_retval)
{
  *_retval = 0;
  return NS_ERROR_NOT_IMPLEMENTED;
} //SetTag

//-----------------------------------------------------------------------------
/* void GetTags (in PRUint32 tagCount, [array, size_is (tagCount)] in wstring tags, out PRUint32 valueCount, [array, size_is (valueCount), retval] out wstring values); */
NS_IMETHODIMP sbMetadataHandlerID3::GetTags(PRUint32 tagCount, const PRUnichar **tags, PRUint32 *valueCount, PRUnichar ***values)
{
  return NS_ERROR_NOT_IMPLEMENTED;
} //GetTags

//-----------------------------------------------------------------------------
/* PRInt32 SetTags (in PRUint32 tagCount, [array, size_is (tagCount)] in wstring tags, in PRUint32 valueCount, [array, size_is (valueCount)] in wstring values); */
NS_IMETHODIMP sbMetadataHandlerID3::SetTags(PRUint32 tagCount, const PRUnichar **tags, PRUint32 valueCount, const PRUnichar **values, PRInt32 *_retval)
{
  *_retval = 0;
  return NS_ERROR_NOT_IMPLEMENTED;
} //SetTags

//-----------------------------------------------------------------------------
PRInt32 sbMetadataHandlerID3::ReadTag(ID3_Tag &tag)
{
  PRInt32 ret = 0;

  nsString strKey;
  nsString strValue;
  PRInt32 type = 0; // Always string, for now.

  ID3_Tag::Iterator *itFrame = tag.CreateIterator();
  ID3_Frame *pFrame = nsnull;

  while( (pFrame = itFrame->GetNext()) != nsnull)
  {
    switch(pFrame->GetID())
    {
      //No known frame.
      case ID3FID_NOFRAME: strKey = NS_LITERAL_STRING(""); break;

      //Audio encryption.
      case ID3FID_AUDIOCRYPTO: strKey = NS_LITERAL_STRING(""); break;

      //Picture
      case ID3FID_PICTURE: strKey = NS_LITERAL_STRING(""); break;

      //Comments.
      case ID3FID_COMMENT: strKey = NS_LITERAL_STRING("comment"); break;

      //Commercial frame.
      case ID3FID_COMMERCIAL: strKey = NS_LITERAL_STRING(""); break;

      //Encryption method registration.
      case ID3FID_CRYPTOREG: strKey = NS_LITERAL_STRING(""); break;

      //Equalization.
      case ID3FID_EQUALIZATION: strKey = NS_LITERAL_STRING(""); break;

      //Event timing codes.
      case ID3FID_EVENTTIMING: strKey = NS_LITERAL_STRING(""); break;
      
      //General encapsulated object.
      case ID3FID_GENERALOBJECT: strKey = NS_LITERAL_STRING(""); break;

      //Group identification registration.
      case ID3FID_GROUPINGREG: strKey = NS_LITERAL_STRING(""); break;

      //Involved people list.
      case ID3FID_INVOLVEDPEOPLE: strKey = NS_LITERAL_STRING(""); break;

      //Linked information.
      case ID3FID_LINKEDINFO: strKey = NS_LITERAL_STRING(""); break;

      //Music CD identifier.
      case ID3FID_CDID: strKey = NS_LITERAL_STRING(""); break;

      //MPEG location lookup table.
      case ID3FID_MPEGLOOKUP: strKey = NS_LITERAL_STRING(""); break;

      //Ownership frame.
      case ID3FID_OWNERSHIP: strKey = NS_LITERAL_STRING(""); break;

      //Private frame.
      case ID3FID_PRIVATE: strKey = NS_LITERAL_STRING(""); break;

      //Play counter.
      case ID3FID_PLAYCOUNTER: strKey = NS_LITERAL_STRING(""); break;

      //Popularimeter.
      case ID3FID_POPULARIMETER: strKey = NS_LITERAL_STRING(""); break;

      //Position synchronisation frame.
      case ID3FID_POSITIONSYNC: strKey = NS_LITERAL_STRING(""); break;

      //Recommended buffer size.
      case ID3FID_BUFFERSIZE: strKey = NS_LITERAL_STRING(""); break;

      //Relative volume adjustment.
      case ID3FID_VOLUMEADJ: strKey = NS_LITERAL_STRING(""); break;

      //Reverb.
      case ID3FID_REVERB: strKey = NS_LITERAL_STRING(""); break;

      //Synchronized lyric/text.
      case ID3FID_SYNCEDLYRICS: strKey = NS_LITERAL_STRING(""); break;

      //Synchronized tempo codes.
      case ID3FID_SYNCEDTEMPO: strKey = NS_LITERAL_STRING(""); break;

      //Album/Movie/Show title.
      case ID3FID_ALBUM: strKey = NS_LITERAL_STRING("album"); break;

      //BPM (beats per minute).
      case ID3FID_BPM: strKey = NS_LITERAL_STRING("bpm"); break;

      //Composer.
      case ID3FID_COMPOSER: strKey = NS_LITERAL_STRING("composer"); break;

      //Content type.
      case ID3FID_CONTENTTYPE: strKey = NS_LITERAL_STRING("genre"); break;

      //Copyright message.
      case ID3FID_COPYRIGHT: strKey = NS_LITERAL_STRING("copyright_message"); break;

      //Date.
      case ID3FID_DATE: strKey = NS_LITERAL_STRING(""); break;

      //Playlist delay.
      case ID3FID_PLAYLISTDELAY: strKey = NS_LITERAL_STRING(""); break;

      //Encoded by.
      case ID3FID_ENCODEDBY: strKey = NS_LITERAL_STRING("encoded_by"); break;

      //Lyricist/Text writer.
      case ID3FID_LYRICIST: strKey = NS_LITERAL_STRING(""); break;

      //File type.
      case ID3FID_FILETYPE: strKey = NS_LITERAL_STRING(""); break;

      //Time.
      case ID3FID_TIME: strKey = NS_LITERAL_STRING("length"); break;

      //Content group description.
      case ID3FID_CONTENTGROUP: strKey = NS_LITERAL_STRING(""); break;

      //Title/songname/content description.
      case ID3FID_TITLE: strKey = NS_LITERAL_STRING("title"); break;

      //Subtitle/Description refinement.
      case ID3FID_SUBTITLE: strKey = NS_LITERAL_STRING("subtitle"); break;

      //Initial key.
      case ID3FID_INITIALKEY: strKey = NS_LITERAL_STRING("key"); break;

      //Language(s).
      case ID3FID_LANGUAGE: strKey = NS_LITERAL_STRING("language"); break;

      //Length.
      case ID3FID_SONGLEN: strKey = NS_LITERAL_STRING("length"); break;

      //Media type.
      case ID3FID_MEDIATYPE: strKey = NS_LITERAL_STRING("mediatype"); break;

      //Original album/movie/show title.
      case ID3FID_ORIGALBUM: strKey = NS_LITERAL_STRING("original_album"); break;

      //Original filename.
      case ID3FID_ORIGFILENAME: strKey = NS_LITERAL_STRING(""); break;

      //Original lyricist(s)/text writer(s).
      case ID3FID_ORIGLYRICIST: strKey = NS_LITERAL_STRING(""); break;

      //Original artist(s)/performer(s).
      case ID3FID_ORIGARTIST: strKey = NS_LITERAL_STRING("original_artist"); break;

      //Original release year.
      case ID3FID_ORIGYEAR: strKey = NS_LITERAL_STRING("original_year"); break;

      //File owner/licensee.
      case ID3FID_FILEOWNER: strKey = NS_LITERAL_STRING(""); break;

      //Lead performer(s)/Soloist(s).
      case ID3FID_LEADARTIST: strKey = NS_LITERAL_STRING("artist"); break;

      //Band/orchestra/accompaniment.
      case ID3FID_BAND: strKey = NS_LITERAL_STRING("accompaniment"); break;

      //Conductor/performer refinement.
      case ID3FID_CONDUCTOR: strKey = NS_LITERAL_STRING("conductor"); break;

      //Interpreted, remixed, or otherwise modified by.
      case ID3FID_MIXARTIST: strKey = NS_LITERAL_STRING("interpreter_remixer"); break;

      //Part of a set.
      case ID3FID_PARTINSET: strKey = NS_LITERAL_STRING("set_collection"); break;

      //Publisher.
      case ID3FID_PUBLISHER: strKey = NS_LITERAL_STRING("publisher"); break;

      //Track number/Position in set.
      case ID3FID_TRACKNUM: strKey = NS_LITERAL_STRING("track_no"); break;

      //Recording dates.
      case ID3FID_RECORDINGDATES: strKey = NS_LITERAL_STRING(""); break;

      //Internet radio station name.
      case ID3FID_NETRADIOSTATION: strKey = NS_LITERAL_STRING(""); break;

      //Internet radio station owner.
      case ID3FID_NETRADIOOWNER: strKey = NS_LITERAL_STRING(""); break;

      //Size.
      case ID3FID_SIZE: strKey = NS_LITERAL_STRING(""); break;

      //ISRC (international standard recording code).
      case ID3FID_ISRC: strKey = NS_LITERAL_STRING(""); break;

      //Software/Hardware and settings used for encoding.
      case ID3FID_ENCODERSETTINGS: strKey = NS_LITERAL_STRING(""); break;

      //User defined text information.
      case ID3FID_USERTEXT: strKey = NS_LITERAL_STRING(""); break;

      //Year.
      case ID3FID_YEAR: strKey = NS_LITERAL_STRING("year"); break;

      //Unique file identifier.
      case ID3FID_UNIQUEFILEID: strKey = NS_LITERAL_STRING("metadata_uuid"); break;

      //Terms of use.
      case ID3FID_TERMSOFUSE: strKey = NS_LITERAL_STRING("terms_of_use"); break;

      //Unsynchronized lyric/text transcription.
      case ID3FID_UNSYNCEDLYRICS: strKey = NS_LITERAL_STRING("lyrics"); break;

      //Commercial information.
      case ID3FID_WWWCOMMERCIALINFO: strKey = NS_LITERAL_STRING("commercialinfo_url"); break;

      //Copyright/Legal infromation.
      case ID3FID_WWWCOPYRIGHT: strKey = NS_LITERAL_STRING("copyright_url"); break;

      //Official audio file webpage.
      case ID3FID_WWWAUDIOFILE: strKey = NS_LITERAL_STRING(""); break;

      //Official artist/performer webpage.
      case ID3FID_WWWARTIST: strKey = NS_LITERAL_STRING("artist_url"); break;

      //Official audio source webpage.
      case ID3FID_WWWAUDIOSOURCE: strKey = NS_LITERAL_STRING("source_url"); break;

      //Official internet radio station homepage.
      case ID3FID_WWWRADIOPAGE: strKey = NS_LITERAL_STRING("netradio_url"); break;

      //Payment.
      case ID3FID_WWWPAYMENT: strKey = NS_LITERAL_STRING("payment_url"); break;

      //Official publisher webpage.
      case ID3FID_WWWPUBLISHER: strKey = NS_LITERAL_STRING("publisher_url"); break;

      //User defined URL link.
      case ID3FID_WWWUSER: strKey = NS_LITERAL_STRING("user_url"); break;

      //Encrypted meta frame (id3v2.2.x).
      case ID3FID_METACRYPTO: strKey = NS_LITERAL_STRING(""); break;

      //Compressed meta frame (id3v2.2.1).
      case ID3FID_METACOMPRESSION: strKey = NS_LITERAL_STRING(""); break;

      //Last field placeholder.
      case ID3FID_LASTFRAMEID: strKey = NS_LITERAL_STRING(""); break;
    }

    // If we care,
    if ( strKey.Length() )
    {
      // Get the text field.
      ID3_Field* pField = pFrame->GetField(ID3FN_TEXT);
      if (nsnull != pField)
      {
        ID3_TextEnc enc = pField->GetEncoding();
        switch( enc )
        {
          case ID3TE_NONE:
          case ID3TE_ISO8859_1:
          {
            nsCString iso_string( pField->GetRawText() );
#ifdef XP_WIN
            // Just for fun, some systems like to save their values in the current windows codepage.
            // This doesn't guarantee we'll translate them properly, but we'll at least have a better chance.
            int size = MultiByteToWideChar( CP_ACP, 0, iso_string.get(), iso_string.Length(), NULL, 0 );
            PRUnichar *uni_string = reinterpret_cast< PRUnichar * >( nsMemory::Alloc( (size + 1) * sizeof( PRUnichar ) ) );
            int read = MultiByteToWideChar( CP_ACP, 0, iso_string.get(), iso_string.Length(), uni_string, size );
            NS_ASSERTION(size == read, "Win32 ISO8859 conversion failed.");
            uni_string[ size ] = 0;
            strValue.Assign( uni_string );
            nsMemory::Free( uni_string );
#else
            strValue = NS_ConvertUTF8toUTF16(iso_string);
#endif
            break;
          }
          case ID3TE_UTF16BE: // ?? what do we do with big endian?  cry?
          case ID3TE_UTF16:
          {
            nsString u16_string( pField->GetRawUnicodeText() );
            strValue = u16_string;
            break;
          }
          case ID3TE_UTF8:
          {
            nsCString u8_string( pField->GetRawText() );
            strValue = NS_ConvertUTF8toUTF16(u8_string);
            break;
          }
        }
      }
      m_Values->SetValue( strKey.get(), strValue.get(), type );
    }
  }

  // For now, leak this?

//  delete itFrame;

  // We crash when we delete it?

  m_Completed = true;  // And, we're done.
  return ret;
} //ReadTag

//-----------------------------------------------------------------------------
PRInt32 sbMetadataHandlerID3::ReadFrame(ID3_Frame *frame)
{
  PRInt32 ret = 0;

  ID3_Frame::Iterator *itField = frame->CreateIterator();
  ID3_Field* field = nsnull;

  while( (field = itField->GetNext()) != nsnull )
  {
    ReadFields(field);
  }

  delete itField;

  return ret;
} //ReadFrame

//-----------------------------------------------------------------------------
PRInt32 sbMetadataHandlerID3::ReadFields(ID3_Field *field)
{
  PRInt32 ret = 0;

  ID3_FieldType fieldType = field->GetType();
  switch(fieldType)
  {
    case ID3FTY_NONE: break;
    case ID3FTY_INTEGER: break;
    case ID3FTY_BINARY: break;
    case ID3FTY_TEXTSTRING: break;
  }
 
  return ret;
} //ReadField
