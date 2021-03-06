/**
 *  @file
 *  @copyright defined in xmax/LICENSE
 */
#pragma once
#include <fc/exception/exception.hpp>
#include <basetypes.hpp>


constexpr uint64_t PACK_TOKEN_NAME(char C1, char C2, char C3) { 
	return (uint64_t(C1) << 8) | (uint64_t(C2) << 16) | (uint64_t(C3) << 24); 
}

constexpr uint64_t PACK_TOKEN_NAME(char C1, char C2, char C3, char C4) { 
	return (uint64_t(C1) << 8) | (uint64_t(C2) << 16) | (uint64_t(C3) << 24) | (uint64_t(C4) << 32); 
}

constexpr uint64_t PACK_TOKEN_NAME(char C1, char C2, char C3, char C4, char C5) {
	return ((uint64_t(C1) << 8) | (uint64_t(C2) << 16) | (uint64_t(C3) << 24) | (uint64_t(C4) << 32) | (uint64_t(C5) << 40));
}

constexpr uint64_t PACK_TOKEN_NAME(char C1, char C2, char C3, char C4, char C5, char C6) {
	return ((uint64_t(C1) << 8) | (uint64_t(C2) << 16) | (uint64_t(C3) << 24) | (uint64_t(C4) << 32) | (uint64_t(C5) << 40) | (uint64_t(C6) << 48));
}

constexpr uint64_t PACK_TOKEN_NAME(char C1, char C2, char C3, char C4, char C5, char C6, char C7) {
	return ((uint64_t(C1) << 8) | (uint64_t(C2) << 16) | (uint64_t(C3) << 24) | (uint64_t(C4) << 32) | (uint64_t(C5) << 40) | (uint64_t(C6) << 48) | (uint64_t(C7) << 56));
}

#define PACK_TOKEN_NAME(...) PACK_TOKEN_NAME(__VA_ARGS__)
#define MAKE_TOKEN_SYMBOL(DECIMALS, ...) (int64_t(DECIMALS) | PACK_TOKEN_NAME(__VA_ARGS__))


/// xmax with 8 digits of precision
#define MAIN_SYMBOL  MAKE_TOKEN_SYMBOL(8, 'S', 'U', 'P')

/// Defined to be largest power of 10 that fits in 53 bits of precision
#define MAIN_TOKEN_MAX_SHARE_SUPPLY   int64_t(1'000'000'000'000'000ll)

namespace Xmaxplatform { namespace Basetypes {

   //using asset_symbol = uint64_t;
   using share_type   = int64;

   uint64_t token_name_from_string(const string& token_str);
   string token_name_to_string(uint64_t tokename);

   class  asset_symbol
   {
   public:
	   asset_symbol():data(0) {}

	   asset_symbol(const uint64_t& _data) :data(_data) {}

	   asset_symbol(const string& token_str) :data(token_name_from_string(token_str)) {}

	   asset_symbol& operator=(uint64_t n) {
		   data = n;
		   return *this;
	   }

	   asset_symbol& operator=(const std::string& n) {
		   data = token_name_from_string(n);
		   return *this;
	   }
	   asset_symbol& operator=(const char* n) {
		   data = token_name_from_string(n);
		   return *this;
	   }

	   friend bool operator < (const asset_symbol& a, const asset_symbol& b) { return a.data < b.data; }
	   friend bool operator <= (const asset_symbol& a, const asset_symbol& b) { return a.data <= b.data; }
	   friend bool operator > (const asset_symbol& a, const asset_symbol& b) { return a.data > b.data; }
	   friend bool operator >=(const asset_symbol& a, const asset_symbol& b) { return a.data >= b.data; }
	   friend bool operator == (const asset_symbol& a, const asset_symbol& b) { return a.data == b.data; }

	   friend bool operator == (const asset_symbol& a, uint64_t b) { return a.data == b; }
	   friend bool operator != (const asset_symbol& a, uint64_t b) { return a.data != b; }

	   friend bool operator != (const asset_symbol& a, const uint64_t& b) { return a.data != b; }

	   template<typename Stream>
	   friend Stream& operator << (Stream& out, const asset_symbol& n) {
		   return out << std::string(n);
	   }

	   std::string to_string() const {
		   return token_name_to_string(data);
	   }

	   explicit operator std::string()const
	   {
		   return to_string();
	   }

	   static asset_symbol from_string(const string& from);

	   uint64_t data;
   };


   inline uint64_t token_name_from_string(const string& token_str) {
	   auto char_count = token_str.size();
	   XMAX_ASSERT(char_count <= 7 && char_count >= 3, invalid_field_name_exception, "Token name invalide: ${name}", ("name", token_str));
	   uint64_t res = 0;
	   for (unsigned int i = 0; i < char_count; ++i)
	   {
		   res |= token_str[i] << 8 * (i );
	   }	   
	   return uint64_t(res);
   }

   inline string token_name_to_string(uint64_t tokename) {
	   string ret;
	   uint64_t mask = 255;
	   for (unsigned int i = 0; i < 8; ++i)
	   {	  
		   char temp = tokename & mask;
		   tokename = tokename >> 8;
		   ret.push_back(temp);
	   }
	   return std::move(ret);
   }

   struct asset
   {
      asset(share_type a = 0, asset_symbol id = MAIN_SYMBOL)
      :amount(a),symbol(id){}

      share_type   amount;
      asset_symbol symbol;

      double to_real()const { return static_cast<double>(amount) / precision(); }

      uint8_t     decimals()const;
      string      symbol_name()const;
      int64_t     precision()const;
      void        set_decimals(uint8_t d);

      static asset from_string(const string& from);
      string       to_string()const;

      asset& operator += (const asset& o)
      {
         FC_ASSERT(symbol == o.symbol);
         amount += o.amount;
         return *this;
      }

      asset& operator -= (const asset& o)
      {
         FC_ASSERT(symbol == o.symbol);
         amount -= o.amount;
         return *this;
      }
      asset operator -()const { return asset(-amount, symbol); }

      friend bool operator == (const asset& a, const asset& b)
      {
         return std::tie(a.symbol, a.amount) == std::tie(b.symbol, b.amount);
      }
      friend bool operator < (const asset& a, const asset& b)
      {
         FC_ASSERT(a.symbol == b.symbol);
         return std::tie(a.amount,a.symbol) < std::tie(b.amount,b.symbol);
      }
      friend bool operator <= (const asset& a, const asset& b) { return (a == b) || (a < b); }
      friend bool operator != (const asset& a, const asset& b) { return !(a == b); }
      friend bool operator > (const asset& a, const asset& b)  { return !(a <= b); }
      friend bool operator >= (const asset& a, const asset& b) { return !(a < b);  }

      friend asset operator - (const asset& a, const asset& b) {
         FC_ASSERT(a.symbol == b.symbol);
         return asset(a.amount - b.amount, a.symbol);
      }

      friend asset operator + (const asset& a, const asset& b) {
         FC_ASSERT(a.symbol == b.symbol);
         return asset(a.amount + b.amount, a.symbol);
      }

      friend std::ostream& operator << (std::ostream& out, const asset& a) { return out << a.to_string(); }

   };

   struct price
   {
      asset base;
      asset quote;

      price(const asset& base = asset(), const asset quote = asset())
      :base(base),quote(quote){}

      static price max(asset_symbol base, asset_symbol quote);
      static price min(asset_symbol base, asset_symbol quote);

      price max()const { return price::max(base.symbol, quote.symbol); }
      price min()const { return price::min(base.symbol, quote.symbol); }

      double to_real()const { return base.to_real() / quote.to_real(); }

      bool is_null()const;
      void validate()const;
   };

   price operator / (const asset& base, const asset& quote);
   inline price operator~(const price& p) { return price{p.quote,p.base}; }

   bool  operator <  (const asset& a, const asset& b);
   bool  operator <= (const asset& a, const asset& b);
   bool  operator <  (const price& a, const price& b);
   bool  operator <= (const price& a, const price& b);
   bool  operator >  (const price& a, const price& b);
   bool  operator >= (const price& a, const price& b);
   bool  operator == (const price& a, const price& b);
   bool  operator != (const price& a, const price& b);
   asset operator *  (const asset& a, const price& b);

}} // namespace Xmaxplatform::Basetypes

namespace fc {
    inline void to_variant(const Xmaxplatform::Basetypes::asset& var, fc::variant& vo) { vo = var.to_string(); }
    inline void from_variant(const fc::variant& var, Xmaxplatform::Basetypes::asset& vo) {
       vo = Xmaxplatform::Basetypes::asset::from_string(var.get_string());
    }
	inline void to_variant(const Xmaxplatform::Basetypes::asset_symbol& c, fc::variant& v) { v = std::string(c); }
	inline void from_variant(const fc::variant& v, Xmaxplatform::Basetypes::asset_symbol& check) { check = v.get_string(); }
}

FC_REFLECT(Xmaxplatform::Basetypes::asset_symbol, (data))
FC_REFLECT(Xmaxplatform::Basetypes::asset, (amount)(symbol))
FC_REFLECT(Xmaxplatform::Basetypes::price, (base)(quote))

