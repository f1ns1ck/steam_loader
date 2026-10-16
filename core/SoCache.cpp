#include "SoCache.hpp"

#include <chrono>
#include <fstream>
#include <iterator>

namespace SoCache
{
	namespace
	{
		// a field of the protobuf wire: the varint (and fixed) values as numbers, the lengths as a span
		struct Field_t
		{
			uint32_t Number = 0;
			uint32_t Type = 0;
			uint64_t Value = 0;
			const uint8_t* pData = nullptr;
			size_t Size = 0;
		};

		class Reader_t
		{
		public:
			Reader_t( const uint8_t* pData , size_t Size ) : m_p( pData ) , m_End( pData + Size ) {}

			auto Next( Field_t& Out ) -> bool
			{
				if ( m_p >= m_End )
					return false;

				uint64_t Key = 0;

				if ( !Varint( Key ) )
					return false;

				Out = {};
				Out.Number = static_cast<uint32_t>( Key >> 3 );
				Out.Type = static_cast<uint32_t>( Key & 7 );

				switch ( Out.Type )
				{
					case 0:
						return Varint( Out.Value );
					case 1:
						return Fixed( 8 , Out.Value );
					case 5:
						return Fixed( 4 , Out.Value );
					case 2:
					{
						uint64_t Length = 0;

						if ( !Varint( Length ) || Length > static_cast<uint64_t>( m_End - m_p ) )
							return false;

						Out.pData = m_p;
						Out.Size = static_cast<size_t>( Length );
						m_p += Length;
						return true;
					}
					default:
						return false;
				}
			}

		private:
			auto Varint( uint64_t& Out ) -> bool
			{
				Out = 0;

				for ( int Shift = 0; Shift < 64 && m_p < m_End; Shift += 7 )
				{
					const uint8_t Byte = *m_p++;
					Out |= static_cast<uint64_t>( Byte & 0x7F ) << Shift;

					if ( !( Byte & 0x80 ) )
						return true;
				}

				return false;
			}

			auto Fixed( size_t Bytes , uint64_t& Out ) -> bool
			{
				if ( static_cast<size_t>( m_End - m_p ) < Bytes )
					return false;

				Out = 0;

				for ( size_t i = 0; i < Bytes; i++ )
					Out |= static_cast<uint64_t>( m_p[i] ) << ( 8 * i );

				m_p += Bytes;
				return true;
			}

			const uint8_t* m_p;
			const uint8_t* m_End;
		};

		auto ReadAccountObject( const uint8_t* pData , size_t Size , Account_t& Out ) -> void
		{
			Reader_t Reader( pData , Size );
			Field_t F;

			while ( Reader.Next( F ) )
			{
				if ( F.Type != 0 )
					continue;

				const auto V32 = static_cast<uint32_t>( F.Value );

				switch ( F.Number )
				{
					case 2: Out.Flags = F.Value; break;
					case 3: Out.Wins = V32; break;
					case 4: Out.Losses = V32; break;
					case 5: Out.Kills = V32; break;
					case 6: Out.MostPlayedHero = V32; break;
					case 10: Out.MmBanUntil = V32; break;
					case 11: Out.CommsBanUntil = V32; break;
					case 12: Out.LowPriorityGames = V32; break;
					case 13: Out.ReportBanUntil = V32; break;
					case 15: Out.PriorityTokens = V32; break;
					case 16: Out.PriorityTokenProgress = V32; break;
					case 17: Out.PriorityTokensEarned = V32; break;
					case 18: Out.BrawlWins = V32; break;
					case 19: Out.BrawlLosses = V32; break;
					case 20: Out.BrawlKills = V32; break;
					case 21: Out.LastMatchTime = V32; break;
					case 22: Out.Privacy = V32; break;
					default: break;
				}
			}
		}

		// a cache (the field 2 of the file, or the file itself): its objects
		auto ReadCache( const uint8_t* pData , size_t Size , Account_t& Out ) -> bool
		{
			Reader_t Reader( pData , Size );
			Field_t F;
			bool Found = false;

			while ( Reader.Next( F ) )
			{
				if ( F.Number != 4 || F.Type != 2 )
					continue;

				Reader_t Object( F.pData , F.Size );
				Field_t O;
				int64_t Type = -1;
				const uint8_t* pObject = nullptr;
				size_t ObjectSize = 0;

				while ( Object.Next( O ) )
				{
					if ( O.Number == 1 && O.Type == 0 )
						Type = static_cast<int64_t>( O.Value );
					else if ( O.Number == 2 && O.Type == 2 )
					{
						pObject = O.pData;
						ObjectSize = O.Size;
					}
				}

				if ( Type == kAccountObjectType && pObject )
				{
					ReadAccountObject( pObject , ObjectSize , Out );
					Found = true;
				}
			}

			return Found;
		}

		// cache_<id>.soc -> the id, 0 - another file
		template <class Char>
		auto CacheId( const std::basic_string<Char>& Name ) -> uint32_t
		{
			static constexpr char kPrefix[] = "cache_";
			static constexpr char kSuffix[] = ".soc";

			if ( Name.size() <= 6 + 4 )
				return 0;

			for ( size_t i = 0; i < 6; i++ )
			{
				if ( Name[i] != static_cast<Char>( kPrefix[i] ) )
					return 0;
			}

			for ( size_t i = 0; i < 4; i++ )
			{
				if ( Name[Name.size() - 4 + i] != static_cast<Char>( kSuffix[i] ) )
					return 0;
			}

			uint64_t Id = 0;

			for ( size_t i = 6; i < Name.size() - 4; i++ )
			{
				if ( Name[i] < static_cast<Char>( '0' ) || Name[i] > static_cast<Char>( '9' ) )
					return 0;

				Id = Id * 10 + static_cast<uint64_t>( Name[i] - static_cast<Char>( '0' ) );

				if ( Id > 0xFFFFFFFFULL )
					return 0;
			}

			return static_cast<uint32_t>( Id );
		}
	}

	auto Read( const std::filesystem::path& Path , uint32_t AccountId , Account_t& Out ) -> bool
	{
		Out = {};
		Out.AccountId = AccountId;
		Out.Path = Path;

		std::ifstream File( Path , std::ios::binary );

		if ( !File )
			return false;

		const std::vector<uint8_t> Data( ( std::istreambuf_iterator<char>( File ) ) , std::istreambuf_iterator<char>() );

		if ( Data.empty() || Data.size() > 16 * 1024 * 1024 )
			return false;

		std::error_code Ec;
		const auto Time = std::filesystem::last_write_time( Path , Ec );

		// file_clock -> system_clock through now (to_sys is not in MSVC, clock_cast is not in the older GCC)
		if ( !Ec )
		{
			const auto System = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>( Time - std::filesystem::file_time_type::clock::now() );
			Out.FileTime = std::chrono::duration_cast<std::chrono::seconds>( System.time_since_epoch() ).count();
		}

		// the file: the field 2 is the cache (seen so far); a file that is the cache itself is read too
		Reader_t Reader( Data.data() , Data.size() );
		Field_t F;

		while ( Reader.Next( F ) )
		{
			if ( F.Number == 2 && F.Type == 2 && ReadCache( F.pData , F.Size , Out ) )
			{
				Out.Valid = true;
				return true;
			}
		}

		Out.Valid = ReadCache( Data.data() , Data.size() , Out );
		return Out.Valid;
	}

	auto Scan( const std::filesystem::path& CitadelDir ) -> std::vector<Account_t>
	{
		std::vector<Account_t> Accounts;

		if ( CitadelDir.empty() )
			return Accounts;

		std::error_code Ec;

		for ( std::filesystem::directory_iterator It( CitadelDir , Ec ) , End; !Ec && It != End; It.increment( Ec ) )
		{
			const uint32_t Id = CacheId( It->path().filename().native() );

			if ( !Id )
				continue;

			Account_t Account;
			Read( It->path() , Id , Account );
			Accounts.push_back( std::move( Account ) );
		}

		return Accounts;
	}
}
