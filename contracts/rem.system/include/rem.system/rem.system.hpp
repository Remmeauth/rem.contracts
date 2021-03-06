#pragma once

#include <eosio/asset.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <rem.system/native.hpp>

#include <deque>
#include <optional>
#include <string>
#include <type_traits>

#ifdef CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX
#undef CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX
#endif
// CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX macro determines whether ramfee and namebid proceeds are
// channeled to REX pool. In order to stop these proceeds from being channeled, the macro must
// be set to 0.
#define CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX 1

namespace eosiosystem {

   using eosio::asset;
   using eosio::block_timestamp;
   using eosio::check;
   using eosio::const_mem_fun;
   using eosio::datastream;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::name;
   using eosio::same_payer;
   using eosio::symbol;
   using eosio::symbol_code;
   using eosio::time_point;
   using eosio::time_point_sec;
   using eosio::unsigned_int;

   template<typename E, typename F>
   static inline auto has_field( F flags, E field )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, bool>
   {
      return ( (flags & static_cast<F>(field)) != 0 );
   }

   template<typename E, typename F>
   static inline auto set_field( F flags, E field, bool value = true )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, F >
   {
      if( value )
         return ( flags | static_cast<F>(field) );
      else
         return ( flags & ~static_cast<F>(field) );
   }

   static constexpr uint32_t seconds_per_year      = 52 * 7 * 24 * 3600;
   static constexpr uint32_t seconds_per_day       = 24 * 3600;
   static constexpr int64_t  useconds_per_year     = int64_t(seconds_per_year) * 1000'000ll;
   static constexpr int64_t  useconds_per_day      = int64_t(seconds_per_day) * 1000'000ll;
   static constexpr uint32_t blocks_per_day        = 2 * seconds_per_day; // half seconds per day

   static constexpr int64_t  min_activated_stake   = 150'000'000'0000;
   static constexpr int64_t  min_pervote_daily_pay = 100'0000;
   static constexpr uint32_t refund_delay_sec      = 3 * seconds_per_day;

   static constexpr int64_t  inflation_precision           = 100;     // 2 decimals
   static constexpr int64_t  default_annual_rate           = 0;       // 5% annual rate
   static constexpr int64_t  pay_factor_precision          = 10000;
   static constexpr int64_t  default_inflation_pay_factor  = 50000;   // producers pay share = 10000 / 50000 = 20% of the inflation
   static constexpr int64_t  default_votepay_factor        = 40000;   // per-block pay share = 10000 / 40000 = 25% of the producer pay

   static constexpr int64_t  min_account_ram = 3800; // 3742 bytes - is size of the new user account for current version
   static constexpr uint64_t account_usd_price = 5000; // the price for creating a new account in USD (default = $0.5)

   /**
    *
    * @defgroup eosiosystem rem.system
    * @ingroup eosiocontracts
    * rem.system contract defines the structures and actions needed for blockchain's core functionality.
    * - Users can stake tokens for CPU and Network bandwidth, and then vote for producers or
    *    delegate their vote to a proxy.
    * - Producers register in order to be voted for, and can claim per-block and per-vote rewards.
    * - Users can buy and sell RAM at a market-determined price.
    * - Users can bid on premium names.
    * - A resource exchange system (REX) allows token holders to lend their tokens,
    *    and users to rent CPU and Network resources in return for a market-determined fee.
    * @{
    */

   /**
    * A name bid.
    *
    * @details A name bid consists of:
    * - a `newname` name that the bid is for
    * - a `high_bidder` account name that is the one with the highest bid so far
    * - the `high_bid` which is amount of highest bid
    * - and `last_bid_time` which is the time of the highest bid
    */
   struct [[eosio::table, eosio::contract("rem.system")]] name_bid {
     name            newname;
     name            high_bidder;
     int64_t         high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
     time_point      last_bid_time;

     uint64_t primary_key()const { return newname.value;                    }
     uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
   };

   /**
    * A bid refund.
    *
    * @details A bid refund is defined by:
    * - the `bidder` account name owning the refund
    * - the `amount` to be refunded
    */
   struct [[eosio::table, eosio::contract("rem.system")]] bid_refund {
      name         bidder;
      asset        amount;

      uint64_t primary_key()const { return bidder.value; }
   };

   /**
    * Name bid table
    *
    * @details The name bid table is storing all the `name_bid`s instances.
    */
   typedef eosio::multi_index< "namebids"_n, name_bid,
                               indexed_by<"highbid"_n, const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>  >
                             > name_bid_table;

   /**
    * Bid refund table.
    *
    * @details The bid refund table is storing all the `bid_refund`s instances.
    */
   typedef eosio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;

   /**
    * Defines new global state parameters.
    */
   struct [[eosio::table("global"), eosio::contract("rem.system")]] eosio_global_state : eosio::blockchain_parameters {
      uint64_t free_ram()const { return max_ram_size - total_ram_bytes_reserved; }

      symbol               core_symbol;

      uint64_t             max_ram_size = 64ll*1024 * 1024 * 1024;
      uint64_t             min_account_stake = 1000000; // the minimum stake for new created account 100'0000 REM
      uint64_t             total_ram_bytes_reserved = 0;
      int64_t              total_ram_stake = 0;
      //producer name and pervote factor
      std::vector<std::pair<eosio::name, double>> last_schedule;
      std::vector<std::pair<eosio::name, double>> standby;
      uint32_t last_schedule_version = 0;
      block_timestamp current_round_start_time;

      block_timestamp      last_producer_schedule_update;
      time_point           last_pervote_bucket_fill;
      int64_t              perstake_bucket = 0;
      int64_t              pervote_bucket = 0;
      int64_t              perblock_bucket = 0;
      uint32_t             total_unpaid_blocks = 0; /// all blocks which have been produced but not paid
      int64_t              total_guardians_stake = 0;
      int64_t              total_activated_stake = 0;
      time_point           thresh_activated_stake_time;
      uint16_t             last_producer_schedule_size = 0;
      double               total_producer_vote_weight = 0; /// the sum of all producer votes
      double               total_active_producer_vote_weight = 0; /// the sum of top 21 producer votes
      block_timestamp      last_name_close;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE_DERIVED( eosio_global_state, eosio::blockchain_parameters, (core_symbol)(max_ram_size)(min_account_stake)
                                (total_ram_bytes_reserved)(total_ram_stake)(last_schedule)(standby)(last_schedule_version)
                                (current_round_start_time) (last_producer_schedule_update)(last_pervote_bucket_fill)
                                (perstake_bucket)(pervote_bucket)(perblock_bucket)(total_unpaid_blocks)(total_guardians_stake)
                                (total_activated_stake)(thresh_activated_stake_time)(last_producer_schedule_size)
                                (total_producer_vote_weight)(total_active_producer_vote_weight)(last_name_close) )
   };

   /**
    * Defines new global state parameters added after version 1.0
    */
   struct [[eosio::table("global2"), eosio::contract("rem.system")]] eosio_global_state2 {
      eosio_global_state2(){}

      uint16_t          new_ram_per_block = 0;
      block_timestamp   last_ram_increase;
      block_timestamp   last_block_num; /* deprecated */
      double            total_producer_votepay_share = 0;
      uint8_t           revision = 0; ///< used to track version updates in the future.

      EOSLIB_SERIALIZE( eosio_global_state2, (new_ram_per_block)(last_ram_increase)(last_block_num)
                        (total_producer_votepay_share)(revision) )
   };

   /**
    * Defines new global state parameters added after version 1.3.0
    */
   struct [[eosio::table("global3"), eosio::contract("rem.system")]] eosio_global_state3 {
      eosio_global_state3() { }
      time_point        last_vpay_state_update;
      double            total_vpay_share_change_rate = 0;

      EOSLIB_SERIALIZE( eosio_global_state3, (last_vpay_state_update)(total_vpay_share_change_rate) )
   };

   /**
    * Defines new global state parameters to store inflation rate and distribution
    */
   struct [[eosio::table("global4"), eosio::contract("rem.system")]] eosio_global_state4 {
      eosio_global_state4() { }
      double   continuous_rate;
      int64_t  inflation_pay_factor;
      int64_t  votepay_factor;

      EOSLIB_SERIALIZE( eosio_global_state4, (continuous_rate)(inflation_pay_factor)(votepay_factor) )
   };

   /**
    * Defines new global state parameters to store remme specific parameters
    */
   struct [[eosio::table("globalrem"), eosio::contract("rem.system")]] eosio_global_rem_state {
      double  per_stake_share = 0.6;
      double  per_vote_share  = 0.3;

      name gifter_attr_contract = name{"rem.attr"};
      name gifter_attr_issuer   = name{"rem.attr"};
      name gifter_attr_name     = name{"accgifter"};

      int64_t guardian_stake_threshold = 250'000'0000LL;
      microseconds producer_max_inactivity_time = eosio::minutes(30);
      microseconds producer_inactivity_punishment_period = eosio::days(30);

      microseconds stake_lock_period   = eosio::days(180);
      microseconds stake_unlock_period = eosio::days(180);

      microseconds reassertion_period = eosio::days( 30 );

      EOSLIB_SERIALIZE( eosio_global_rem_state, (per_stake_share)(per_vote_share)
                                                (gifter_attr_contract)(gifter_attr_issuer)(gifter_attr_name)
                                                (guardian_stake_threshold)(producer_max_inactivity_time)(producer_inactivity_punishment_period)
                                                (stake_lock_period)(stake_unlock_period)(reassertion_period) )
   };

   /**
    * Defines new global state parameters to producer schedule rotation
    */
   struct [[eosio::table("rotations"), eosio::contract("rem.system")]] rotation_state {
      time_point   last_rotation_time;
      microseconds rotation_period;
      uint32_t     standby_prods_to_rotate;

      std::vector<eosio::producer_authority> standby_rotation;

      EOSLIB_SERIALIZE( rotation_state, (last_rotation_time)(rotation_period)(standby_prods_to_rotate)(standby_rotation) )
   };

   /**
    * Global state singleton added in version 1.0
    */
   typedef eosio::singleton< "rotations"_n, rotation_state >   rotation_state_singleton;

   /**
    * Defines `producer_info` structure to be stored in `producer_info` table, added after version 1.0
    */
   struct [[eosio::table, eosio::contract("rem.system")]] producer_info {
      name                  owner;
      double                total_votes = 0;
      eosio::public_key     producer_key; /// a packed public key object
      bool                  is_active = true;
      std::string           url;
      uint32_t              current_round_unpaid_blocks = 0;
      uint32_t              unpaid_blocks = 0; //count blocks only from finished rounds
      uint32_t              expected_produced_blocks = 0;
      block_timestamp       last_expected_produced_blocks_update;
      int64_t               pending_pervote_reward = 0;
      time_point            last_claim_time;
      time_point            last_block_time;
      time_point            top21_chosen_time;
      time_point            punished_until;
      uint16_t              location = 0;
      eosio::binary_extension<eosio::block_signing_authority>  producer_authority;

      uint64_t primary_key()const { return owner.value;                             }
      double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
      bool     active()const      { return is_active;                               }
      void     deactivate()       { producer_key = public_key(); producer_authority.reset(); is_active = false; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( producer_info, (owner)(total_votes)(producer_key)(is_active)(url)
                        (current_round_unpaid_blocks)(unpaid_blocks)(expected_produced_blocks)
                        (last_expected_produced_blocks_update)(pending_pervote_reward)(last_claim_time)(last_block_time)(top21_chosen_time)
                        (punished_until)(location)(producer_authority) )
   };

   /**
    * Defines new producer info structure to be stored in new producer info table, added after version 1.3.0
    */
   struct [[eosio::table, eosio::contract("rem.system")]] producer_info2 {
      name            owner;
      double          votepay_share = 0;
      time_point      last_votepay_share_update;

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( producer_info2, (owner)(votepay_share)(last_votepay_share_update) )
   };

   /**
    * Voter info.
    *
    * @details Voter info stores information about the voter:
    * - `owner` the voter
    * - `proxy` the proxy set by the voter, if any
    * - `producers` the producers approved by this voter if no proxy set
    * - `staked` the amount staked
    */
   struct [[eosio::table, eosio::contract("rem.system")]] voter_info {
   public:
      name                owner;     /// the voter
      name                proxy;     /// the proxy set by the voter, if any
      std::vector<name>   producers; /// the producers approved by this voter if no proxy set
      int64_t             staked = 0;
      int64_t             locked_stake = 0;

      double by_stake() const { return staked; }

      /**
       *  Every time a vote is cast we must first "undo" the last vote weight, before casting the
       *  new vote weight.  Vote weight is calculated as:
       *
       *  stated.amount * 2 ^ ( weeks_since_launch/weeks_per_year)
       */
      double              last_vote_weight = 0; /// the vote weight cast the last time the vote was updated
      time_point          stake_lock_time;
      time_point          last_undelegate_time;

      /**
       * Total vote weight delegated to this voter.
       */
      double              proxied_vote_weight= 0; /// the total vote weight delegated to this voter as a proxy
      bool                is_proxy = 0; /// whether the voter is a proxy for others


      uint32_t            flags1 = 0;
      uint32_t            reserved2 = 0;
      eosio::asset        reserved3;

      uint64_t primary_key()const { return owner.value; }

      enum class flags1_fields : uint32_t {
         ram_managed = 1,
         net_managed = 2,
         cpu_managed = 4
      };

      time_point          last_reassertion_time;
      int64_t             pending_perstake_reward = 0;
      time_point          last_claim_time;


      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( voter_info, (owner)(proxy)(producers)(staked)(locked_stake)(last_vote_weight)
                                    (stake_lock_time)(last_undelegate_time)(proxied_vote_weight)(is_proxy)(flags1)(reserved2)(reserved3)
                                    (last_reassertion_time)(pending_perstake_reward)(last_claim_time) )
   };

   struct [[eosio::table, eosio::contract("rem.system")]] user_resources {
      name          owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       own_stake_amount = 0; //controlled by delegatebw and undelegatebw only
      int64_t       free_stake_amount = 0; // some accounts may get free stake for all resources,
                                           // that will decrease when user stake own tokens above the free limits

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(own_stake_amount)(free_stake_amount) )
   };

   /**
    *  Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
    */
   struct [[eosio::table, eosio::contract("rem.system")]] delegated_bandwidth {
      name          from;
      name          to;
      asset         net_weight;
      asset         cpu_weight;

      bool is_empty()const { return net_weight.amount == 0 && cpu_weight.amount == 0; }
      uint64_t  primary_key()const { return to.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight))

   };

   /**
    *  These tables are designed to be constructed in the scope of the relevant user, this
    *  facilitates simpler API for per-user queries
    */
   typedef eosio::multi_index< "delband"_n, delegated_bandwidth > del_bandwidth_table;

   /**
    *  These table is designed to be constructed in the scope of the relevant user, this
    *  facilitates simpler API for per-user queries
    */
   typedef eosio::multi_index< "userres"_n, user_resources >      user_resources_table;

   /**
    * Voters table
    *
    * @details The voters table stores all the `voter_info`s instances, all voters information.
    */
   typedef eosio::multi_index< "voters"_n, voter_info,
                               indexed_by<"bystake"_n, const_mem_fun<voter_info, double, &voter_info::by_stake> >
                             > voters_table;


   /**
    * Defines producer info table added in version 1.0
    */
   typedef eosio::multi_index< "producers"_n, producer_info,
                               indexed_by<"prototalvote"_n, const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                             > producers_table;
   /**
    * Defines new producer info table added in version 1.3.0
    */
   typedef eosio::multi_index< "producers2"_n, producer_info2 > producers_table2;

   /**
    * Global state singleton added in version 1.0
    */
   typedef eosio::singleton< "global"_n, eosio_global_state >   global_state_singleton;
   /**
    * Global state singleton added in version 1.1.0
    */
   typedef eosio::singleton< "global2"_n, eosio_global_state2 > global_state2_singleton;
   /**
    * Global state singleton added in version 1.3
    */
   typedef eosio::singleton< "global3"_n, eosio_global_state3 > global_state3_singleton;
   /**
    * Global state singleton added in version 1.6.x
    */
   typedef eosio::singleton< "global4"_n, eosio_global_state4 > global_state4_singleton;

   typedef eosio::singleton< "globalrem"_n, eosio_global_rem_state > global_rem_state_singleton;


   /**
    * `rex_pool` structure underlying the rex pool table.
    *
    * @details A rex pool table entry is defined by:
    * - `version` defaulted to zero,
    * - `total_lent` total amount of CORE_SYMBOL in open rex_loans
    * - `total_unlent` total amount of CORE_SYMBOL available to be lent (connector),
    * - `total_rent` fees received in exchange for lent  (connector),
    * - `total_lendable` total amount of CORE_SYMBOL that have been lent (total_unlent + total_lent),
    * - `total_rex` total number of REX shares allocated to contributors to total_lendable,
    * - `namebid_proceeds` the amount of CORE_SYMBOL to be transferred from namebids to REX pool,
    * - `loan_num` increments with each new loan.
    */
   struct [[eosio::table,eosio::contract("rem.system")]] rex_pool {
      uint8_t    version = 0;
      asset      total_lent;
      asset      total_unlent;
      asset      total_rent;
      asset      total_lendable;
      asset      total_rex;
      asset      namebid_proceeds;
      uint64_t   loan_num = 0;

      uint64_t primary_key()const { return 0; }
   };

   /**
    * Rex pool table
    *
    * @details The rex pool table is storing the only one instance of rex_pool which it stores
    * the global state of the REX system.
    */
   typedef eosio::multi_index< "rexpool"_n, rex_pool > rex_pool_table;

   /**
    * `rex_fund` structure underlying the rex fund table.
    *
    * @details A rex fund table entry is defined by:
    * - `version` defaulted to zero,
    * - `owner` the owner of the rex fund,
    * - `balance` the balance of the fund.
    */
   struct [[eosio::table,eosio::contract("rem.system")]] rex_fund {
      uint8_t version = 0;
      name    owner;
      asset   balance;

      uint64_t primary_key()const { return owner.value; }
   };

   /**
    * Rex fund table
    *
    * @details The rex fund table is storing all the `rex_fund`s instances.
    */
   typedef eosio::multi_index< "rexfund"_n, rex_fund > rex_fund_table;

   /**
    * `rex_balance` structure underlying the rex balance table.
    *
    * @details A rex balance table entry is defined by:
    * - `version` defaulted to zero,
    * - `owner` the owner of the rex fund,
    * - `vote_stake` the amount of CORE_SYMBOL currently included in owner's vote,
    * - `rex_balance` the amount of REX owned by owner,
    * - `matured_rex` matured REX available for selling.
    */
   struct [[eosio::table,eosio::contract("rem.system")]] rex_balance {
      uint8_t version = 0;
      name    owner;
      asset   vote_stake;
      asset   rex_balance;
      int64_t matured_rex = 0;
      std::deque<std::pair<time_point_sec, int64_t>> rex_maturities; /// REX daily maturity buckets

      uint64_t primary_key()const { return owner.value; }
   };

   /**
    * Rex balance table
    *
    * @details The rex balance table is storing all the `rex_balance`s instances.
    */
   typedef eosio::multi_index< "rexbal"_n, rex_balance > rex_balance_table;

   /**
    * `rex_loan` structure underlying the `rex_cpu_loan_table` and `rex_net_loan_table`.
    *
    * @details A rex net/cpu loan table entry is defined by:
    * - `version` defaulted to zero,
    * - `from` account creating and paying for loan,
    * - `receiver` account receiving rented resources,
    * - `payment` SYS tokens paid for the loan,
    * - `balance` is the amount of SYS tokens available to be used for loan auto-renewal,
    * - `total_staked` total amount staked,
    * - `loan_num` loan number/id,
    * - `expiration` the expiration time when loan will be either closed or renewed
    *       If payment <= balance, the loan is renewed, and closed otherwise.
    */
   struct [[eosio::table,eosio::contract("rem.system")]] rex_loan {
      uint8_t             version = 0;
      name                from;
      name                receiver;
      asset               payment;
      asset               balance;
      asset               total_staked;
      uint64_t            loan_num;
      eosio::time_point   expiration;

      uint64_t primary_key()const { return loan_num;                   }
      uint64_t by_expr()const     { return expiration.elapsed.count(); }
      uint64_t by_owner()const    { return from.value;                 }
   };

   /**
    * Rex cpu loan table
    *
    * @details The rex cpu loan table is storing all the `rex_loan`s instances for cpu, indexed by loan number, expiration and owner.
    */
   typedef eosio::multi_index< "cpuloan"_n, rex_loan,
                               indexed_by<"byexpr"_n,  const_mem_fun<rex_loan, uint64_t, &rex_loan::by_expr>>,
                               indexed_by<"byowner"_n, const_mem_fun<rex_loan, uint64_t, &rex_loan::by_owner>>
                             > rex_cpu_loan_table;

   /**
    * Rex net loan table
    *
    * @details The rex net loan table is storing all the `rex_loan`s instances for net, indexed by loan number, expiration and owner.
    */
   typedef eosio::multi_index< "netloan"_n, rex_loan,
                               indexed_by<"byexpr"_n,  const_mem_fun<rex_loan, uint64_t, &rex_loan::by_expr>>,
                               indexed_by<"byowner"_n, const_mem_fun<rex_loan, uint64_t, &rex_loan::by_owner>>
                             > rex_net_loan_table;

   struct [[eosio::table,eosio::contract("rem.system")]] rex_order {
      uint8_t             version = 0;
      name                owner;
      asset               rex_requested;
      asset               proceeds;
      asset               stake_change;
      eosio::time_point   order_time;
      bool                is_open = true;

      void close()                { is_open = false;    }
      uint64_t primary_key()const { return owner.value; }
      uint64_t by_time()const     { return is_open ? order_time.elapsed.count() : std::numeric_limits<uint64_t>::max(); }
   };

   /**
    * Rex order table
    *
    * @details The rex order table is storing all the `rex_order`s instances, indexed by owner and time and owner.
    */
   typedef eosio::multi_index< "rexqueue"_n, rex_order,
                               indexed_by<"bytime"_n, const_mem_fun<rex_order, uint64_t, &rex_order::by_time>>> rex_order_table;

   struct rex_order_outcome {
      bool success;
      asset proceeds;
      asset stake_change;
   };

   /**
    * The EOSIO system contract.
    *
    * @details The EOSIO system contract governs ram market, voters, producers, global state.
    */
   class [[eosio::contract("rem.system")]] system_contract : public native {

      private:
         voters_table            _voters;
         producers_table         _producers;
         producers_table2        _producers2;
         global_state_singleton  _global;
         global_state2_singleton _global2;
         global_state3_singleton _global3;
         global_state4_singleton _global4;
         global_rem_state_singleton _globalrem;
         eosio_global_state      _gstate;
         eosio_global_state2     _gstate2;
         eosio_global_state3     _gstate3;
         eosio_global_state4     _gstate4;
         eosio_global_rem_state  _gremstate;
         rex_pool_table          _rexpool;
         rex_fund_table          _rexfunds;
         rex_balance_table       _rexbalance;
         rex_order_table         _rexorders;

         rotation_state_singleton _rotation;
         rotation_state           _grotation;

      public:
         static constexpr eosio::name active_permission{"active"_n};
         static constexpr eosio::name token_account{"rem.token"_n};
         static constexpr eosio::name ram_account{"rem.ram"_n};
         static constexpr eosio::name ramfee_account{"rem.ramfee"_n};
         static constexpr eosio::name stake_account{"rem.stake"_n};
         static constexpr eosio::name swap_account{"rem.swap"_n};
         static constexpr eosio::name utils_account{"rem.utils"_n};
         static constexpr eosio::name bpay_account{"rem.bpay"_n};
         static constexpr eosio::name spay_account{"rem.spay"_n};
         static constexpr eosio::name vpay_account{"rem.vpay"_n};
         static constexpr eosio::name oracle_account{"rem.oracle"_n};
         static constexpr eosio::name names_account{"rem.names"_n};
         static constexpr eosio::name saving_account{"rem.saving"_n};
         static constexpr eosio::name rex_account{"rem.rex"_n};
         static constexpr eosio::name null_account{"rem.null"_n};
         static constexpr eosio::name rem_usd_pair{"rem.usd"_n};
         static constexpr symbol rex_symbol = symbol(symbol_code("REX"), 4);

         static constexpr uint8_t max_block_producers      = 21;


         /**
          * System contract constructor.
          *
          * @details Constructs a system contract based on self account, code account and data.
          *
          * @param s    - The current code account that is executing the action,
          * @param code - The original code account that executed the action,
          * @param ds   - The contract data represented as an `eosio::datastream`.
          */
         system_contract( name s, name code, datastream<const char*> ds );
         ~system_contract();

         /**
          * Returns the core symbol by system account name
          *
          * @param system_account - the system account to get the core symbol for.
          */
         static symbol get_core_symbol( name system_account = "rem"_n ) {
            global_state_singleton _global(system_account, system_account.value);
            check( _global.exists(), "system contract must first be initialized" );
            const auto _gstate  = _global.get();
            const static auto sym = _gstate.core_symbol;
            return sym;
         }

       [[eosio::action]]
       void setrwrdratio( double stake_share, double vote_share );

       [[eosio::action]]
       void setlockperiod( uint64_t period_in_days);

       [[eosio::action]]
       void setunloperiod( uint64_t period_in_days);

       [[eosio::action]]
       void setinacttime( uint64_t period_in_minutes );

       [[eosio::action]]
       void setpnshperiod( uint64_t period_in_days );

       [[eosio::action]]
       void punishprod( const name& producer );


        // Actions:
        /**
         * setgiftcontra - set contract that owns attribute that allows to create accounts with gifted resources
         * setgiftiss    - set issuer that owns attribute that allows to create accounts with gifted resources
         * setgiftattr   - set attribute name that allows to create accounts with gifted resources
         */
       [[eosio::action]]
       void setgiftcontra( name value );
       [[eosio::action]]
       void setgiftiss( name value);
       [[eosio::action]]
       void setgiftattr( name value );


         // Actions:
         /**
          * Init action.
          *
          * @details Init action initializes the system contract for a version and a symbol.
          * Only succeeds when:
          * - version is 0 and
          * - symbol is found and
          * - system token supply is greater than 0,
          * - and system contract wasn’t already been initialized.
          *
          * @param version - the version, has to be 0,
          * @param core - the system symbol.
          */
         [[eosio::action]]
         void init( unsigned_int version, const symbol& core );


         /**
          * New account action
          *
          * @details Called after a new account is created. This code enforces resource-limits rules
          * for new accounts as well as new account naming conventions.
          *
          * 1. accounts cannot contain '.' symbols which forces all acccounts to be 12
          * characters long without '.' until a future account auction process is implemented
          * which prevents name squatting.
          *
          * 2. new accounts must stake a minimal number of tokens (as set in system parameters)
          * therefore, this method will execute an inline buyram from receiver for newacnt in
          * an amount equal to the current new account creation fee.
          */
         [[eosio::action]]
         void newaccount( const name&       creator,
                          const name&       name,
                          ignore<authority> owner,
                          ignore<authority> active);

         /**
          * On block action.
          *
          * @details This special action is triggered when a block is applied by the given producer
          * and cannot be generated from any other source. It is used to pay producers and calculate
          * missed blocks of other producers. Producer pay is deposited into the producer's stake
          * balance and can be withdrawn over time. If blocknum is the start of a new round this may
          * update the active producer config from the producer votes.
          *
          * @param header - the block header produced.
          */
         [[eosio::action]]
         void onblock( ignore<block_header> header );

         /**
          * Set account limits action.
          *
          * @details Set the resource limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param ram_bytes - ram limit in absolute bytes,
          * @param net_weight - fractionally proportionate net limit of available resources based on (weight / total_weight_of_all_accounts),
          * @param cpu_weight - fractionally proportionate cpu limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[eosio::action]]
         void setalimits( const name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight );

         /**
          * Activates a protocol feature.
          *
          * @details Activates a protocol feature
          *
          * @param feature_digest - hash of the protocol feature to activate.
          */
         [[eosio::action]]
         void activate( const eosio::checksum256& feature_digest );

         // functions defined in delegate_bandwidth.cpp

         /**
          * Delegate bandwidth and/or cpu action.
          *
          * @details Stakes SYS from the balance of `from` for the benefit of `receiver`.
          *
          * @param from - the account to delegate bandwidth from, that is, the account holding
          *    tokens to be staked,
          * @param receiver - the account to delegate bandwith to, that is, the account to
          *    whose resources staked tokens are added
          * @param stake_quantity - tokens staked for resources: NET, CPU and RAM
          * @param transfer - if true, ownership of staked tokens is transfered to `receiver`.
          *
          * @post All producers `from` account has voted for will have their votes updated immediately.
          */
         [[eosio::action]]
         void delegatebw( const name& from, const name& receiver,
                          const asset& stake_quantity, bool transfer );

         /**
          * Setrex action.
          *
          * @details Sets total_rent balance of REX pool to the passed value.
          * @param balance - amount to set the REX pool balance.
          */
         [[eosio::action]]
         void setrex( const asset& balance );

         /**
          * Deposit to REX fund action.
          *
          * @details Deposits core tokens to user REX fund.
          * All proceeds and expenses related to REX are added to or taken out of this fund.
          * An inline transfer from 'owner' liquid balance is executed.
          * All REX-related costs and proceeds are deducted from and added to 'owner' REX fund,
          *    with one exception being buying REX using staked tokens.
          * Storage change is billed to 'owner'.
          *
          * @param owner - REX fund owner account,
          * @param amount - amount of tokens to be deposited.
          */
         [[eosio::action]]
         void deposit( const name& owner, const asset& amount );

         /**
          * Withdraw from REX fund action.
          *
          * @details Withdraws core tokens from user REX fund.
          * An inline token transfer to user balance is executed.
          *
          * @param owner - REX fund owner account,
          * @param amount - amount of tokens to be withdrawn.
          */
         [[eosio::action]]
         void withdraw( const name& owner, const asset& amount );

         /**
          * Buyrex action.
          *
          * @details Buys REX in exchange for tokens taken out of user's REX fund by transfering
          * core tokens from user REX fund and converts them to REX stake. By buying REX, user is
          * lending tokens in order to be rented as CPU or NET resourses.
          * Storage change is billed to 'from' account.
          *
          * @param from - owner account name,
          * @param amount - amount of tokens taken out of 'from' REX fund.
          *
          * @pre A voting requirement must be satisfied before action can be executed.
          * @pre User must vote for at least 21 producers or delegate vote to proxy before buying REX.
          *
          * @post User votes are updated following this action.
          * @post Tokens used in purchase are added to user's voting power.
          * @post Bought REX cannot be sold before 4 days counting from end of day of purchase.
          */
         [[eosio::action]]
         void buyrex( const name& from, const asset& amount );

         /**
          * Unstaketorex action.
          *
          * @details Use staked core tokens to buy REX.
          * Storage change is billed to 'owner' account.
          *
          * @param owner - owner of staked tokens,
          * @param receiver - account name that tokens have previously been staked to,
          * @param from_net - amount of tokens to be unstaked from NET bandwidth and used for REX purchase,
          * @param from_cpu - amount of tokens to be unstaked from CPU bandwidth and used for REX purchase.
          *
          * @pre A voting requirement must be satisfied before action can be executed.
          * @pre User must vote for at least 21 producers or delegate vote to proxy before buying REX.
          *
          * @post User votes are updated following this action.
          * @post Tokens used in purchase are added to user's voting power.
          * @post Bought REX cannot be sold before 4 days counting from end of day of purchase.
          */
         [[eosio::action]]
         void unstaketorex( const name& owner, const name& receiver, const asset& from_net, const asset& from_cpu );

         /**
          * Sellrex action.
          *
          * @details Sells REX in exchange for core tokens by converting REX stake back into core tokens
          * at current exchange rate. If order cannot be processed, it gets queued until there is enough
          * in REX pool to fill order, and will be processed within 30 days at most. If successful, user
          * votes are updated, that is, proceeds are deducted from user's voting power. In case sell order
          * is queued, storage change is billed to 'from' account.
          *
          * @param from - owner account of REX,
          * @param rex - amount of REX to be sold.
          */
         [[eosio::action]]
         void sellrex( const name& from, const asset& rex );

         /**
          * Cnclrexorder action.
          *
          * @details Cancels unfilled REX sell order by owner if one exists.
          *
          * @param owner - owner account name.
          *
          * @pre Order cannot be cancelled once it's been filled.
          */
         [[eosio::action]]
         void cnclrexorder( const name& owner );

         /**
          * Rentcpu action.
          *
          * @details Use payment to rent as many SYS tokens as possible as determined by market price and
          * stake them for CPU for the benefit of receiver, after 30 days the rented core delegation of CPU
          * will expire. At expiration, if balance is greater than or equal to `loan_payment`, `loan_payment`
          * is taken out of loan balance and used to renew the loan. Otherwise, the loan is closed and user
          * is refunded any remaining balance.
          * Owner can fund or refund a loan at any time before its expiration.
          * All loan expenses and refunds come out of or are added to owner's REX fund.
          *
          * @param from - account creating and paying for CPU loan, 'from' account can add tokens to loan
          *    balance using action `fundcpuloan` and withdraw from loan balance using `defcpuloan`
          * @param receiver - account receiving rented CPU resources,
          * @param loan_payment - tokens paid for the loan, it has to be greater than zero,
          *    amount of rented resources is calculated from `loan_payment`,
          * @param loan_fund - additional tokens can be zero, and is added to loan balance.
          *    Loan balance represents a reserve that is used at expiration for automatic loan renewal.
          */
         [[eosio::action]]
         void rentcpu( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );

         /**
          * Rentnet action.
          *
          * @details Use payment to rent as many SYS tokens as possible as determined by market price and
          * stake them for NET for the benefit of receiver, after 30 days the rented core delegation of NET
          * will expire. At expiration, if balance is greater than or equal to `loan_payment`, `loan_payment`
          * is taken out of loan balance and used to renew the loan. Otherwise, the loan is closed and user
          * is refunded any remaining balance.
          * Owner can fund or refund a loan at any time before its expiration.
          * All loan expenses and refunds come out of or are added to owner's REX fund.
          *
          * @param from - account creating and paying for NET loan, 'from' account can add tokens to loan
          *    balance using action `fundnetloan` and withdraw from loan balance using `defnetloan`,
          * @param receiver - account receiving rented NET resources,
          * @param loan_payment - tokens paid for the loan, it has to be greater than zero,
          *    amount of rented resources is calculated from `loan_payment`,
          * @param loan_fund - additional tokens can be zero, and is added to loan balance.
          *    Loan balance represents a reserve that is used at expiration for automatic loan renewal.
          */
         [[eosio::action]]
         void rentnet( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );

         /**
          * Fundcpuloan action.
          *
          * @details Transfers tokens from REX fund to the fund of a specific CPU loan in order to
          * be used for loan renewal at expiry.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param payment - tokens transfered from REX fund to loan fund.
          */
         [[eosio::action]]
         void fundcpuloan( const name& from, uint64_t loan_num, const asset& payment );

         /**
          * Fundnetloan action.
          *
          * @details Transfers tokens from REX fund to the fund of a specific NET loan in order to
          * be used for loan renewal at expiry.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param payment - tokens transfered from REX fund to loan fund.
          */
         [[eosio::action]]
         void fundnetloan( const name& from, uint64_t loan_num, const asset& payment );

         /**
          * Defcpuloan action.
          *
          * @details Withdraws tokens from the fund of a specific CPU loan and adds them to REX fund.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param amount - tokens transfered from CPU loan fund to REX fund.
          */
         [[eosio::action]]
         void defcpuloan( const name& from, uint64_t loan_num, const asset& amount );

         /**
          * Defnetloan action.
          *
          * @details Withdraws tokens from the fund of a specific NET loan and adds them to REX fund.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param amount - tokens transfered from NET loan fund to REX fund.
          */
         [[eosio::action]]
         void defnetloan( const name& from, uint64_t loan_num, const asset& amount );

         /**
          * Updaterex action.
          *
          * @details Updates REX owner vote weight to current value of held REX tokens.
          *
          * @param owner - REX owner account.
          */
         [[eosio::action]]
         void updaterex( const name& owner );

         /**
          * Rexexec action.
          *
          * @details Processes max CPU loans, max NET loans, and max queued sellrex orders.
          * Action does not execute anything related to a specific user.
          *
          * @param user - any account can execute this action,
          * @param max - number of each of CPU loans, NET loans, and sell orders to be processed.
          */
         [[eosio::action]]
         void rexexec( const name& user, uint16_t max );

         /**
          * Consolidate action.
          *
          * @details Consolidates REX maturity buckets into one bucket that can be sold after 4 days
          * starting from the end of the day.
          *
          * @param owner - REX owner account name.
          */
         [[eosio::action]]
         void consolidate( const name& owner );

         /**
          * Mvtosavings action.
          *
          * @details Moves a specified amount of REX into savings bucket. REX savings bucket
          * never matures. In order for it to be sold, it has to be moved explicitly
          * out of that bucket. Then the moved amount will have the regular maturity
          * period of 4 days starting from the end of the day.
          *
          * @param owner - REX owner account name.
          * @param rex - amount of REX to be moved.
          */
         [[eosio::action]]
         void mvtosavings( const name& owner, const asset& rex );

         /**
          * Mvfrsavings action.
          *
          * @details Moves a specified amount of REX out of savings bucket. The moved amount
          * will have the regular REX maturity period of 4 days.
          *
          * @param owner - REX owner account name.
          * @param rex - amount of REX to be moved.
          */
         [[eosio::action]]
         void mvfrsavings( const name& owner, const asset& rex );

         /**
          * Closerex action.
          *
          * @details Deletes owner records from REX tables and frees used RAM. Owner must not have
          * an outstanding REX balance.
          *
          * @param owner - user account name.
          *
          * @pre If owner has a non-zero REX balance, the action fails; otherwise,
          *    owner REX balance entry is deleted.
          * @pre If owner has no outstanding loans and a zero REX fund balance,
          *    REX fund entry is deleted.
          */
         [[eosio::action]]
         void closerex( const name& owner );

         /**
          * Undelegate bandwitdh action.
          *
          * @details Decreases the total tokens delegated by `from` to `receiver` and/or
          * frees the memory associated with the delegation if there is nothing
          * left to delegate.
          * This will cause an immediate reduction in net/cpu/ram bandwidth of the
          * receiver.
          * A transaction is scheduled to send the tokens back to `from` after
          * the staking period has passed. If existing transaction is scheduled, it
          * will be canceled and a new transaction issued that has the combined
          * undelegated amount.
          * The `from` account loses voting power as a result of this call and
          * all producer tallies are updated.
          *
          * @param from - the account to undelegate bandwidth from, that is,
          *    the account whose tokens will be unstaked,
          * @param receiver - the account to undelegate bandwith to, that is,
          *    the account to whose benefit tokens have been staked,
          * @param unstake_quantity - tokens to be unstaked for resources: NET, CPU and RAM
          *
          * @post Unstaked tokens are transferred to `from` liquid balance via a
          *    deferred transaction with a delay of 3 days.
          * @post If called during the delay period of a previous `undelegatebw`
          *    action, pending action is canceled and timer is reset.
          * @post All producers `from` account has voted for will have their votes updated immediately.
          * @post Bandwidth and storage for the deferred transaction are billed to `from`.
          */
         [[eosio::action]]
         void undelegatebw( const name& from, const name& receiver,
                            const asset& unstake_quantity );

         /**
          * Refund action.
          *
          * @details This action is called after the delegation-period to claim all pending
          * unstaked tokens belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[eosio::action]]
         void refund( const name& owner );

         /**
          * Refund to stake action.
          *
          * @details This action is called after the delegation-period to restake without lock all pending
          * unstaked tokens belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[eosio::action]]
         void refundtostake( const name& owner );

         // functions defined in voting.cpp

         /**
          * Register producer action.
          *
          * @details Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_key - the public key of the block producer, this is the key used by block producer to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer( const name& producer, const public_key& producer_key, const std::string& url, uint16_t location );

         /**
          * Register producer action.
          *
          * @details Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_authority - the weighted threshold multisig block signing authority of the block producer used to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer2( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location );

         /**
          * Unregister producer action.
          *
          * @details Deactivate the block producer with account name `producer`.
          * @param producer - the block producer account to unregister.
          */
         [[eosio::action]]
         void unregprod( const name& producer );

         /**
          * Set ram action.
          *
          * @details Set the ram supply.
          * @param max_ram_size - the amount of ram supply to set.
          */
         [[eosio::action]]
         void setram( uint64_t max_ram_size );

         /**
          * Set min acount stake action.
          *
          * @details Set minimum of new account stake, that is not possible to undelegate.
          * @param min_account_stake - the minimum amount of stake for new account.
          */
         [[eosio::action]]
         void setminstake( uint64_t min_account_stake );

         /**
          * Set activated stake action.
          *
          * @details Set activated stake to equal minimum activated stake.
          */
         [[eosio::action]]
         void setactvstake();

         /**
          * Set ram rate action.

          * @details Sets the rate of increase of RAM in bytes per block. It is capped by the uint16_t to
          * a maximum rate of 3 TB per year. If update_ram_supply hasn't been called for the most recent block,
          * then new ram will be allocated at the old rate up to the present block before switching the rate.
          *
          * @param bytes_per_block - the amount of bytes per block increase to set.
          */
         [[eosio::action]]
         void setramrate( uint16_t bytes_per_block );

         /**
          * Vote producer action.
          *
          * @details Votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account. If voting for a `proxy`, the producer votes will not change until the
          * proxy updates their own vote. Voter can vote for a proxy __or__ a list of at most 30 producers.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param proxy - the proxy to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre If proxy is set then no producers can be voted for
          * @pre If proxy is set then proxy account must exist and be registered as a proxy
          * @pre Every listed producer or proxy must have been previously registered
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some EOS for voting
          * @pre Voter->staked must be up to date
          *
          * @post Every producer previously voted for will have vote reduced by previous vote weight
          * @post Every producer newly voted for will have vote increased by new vote amount
          * @post Prior proxy will proxied_vote_weight decremented by previous vote weight
          * @post New proxy will proxied_vote_weight incremented by new vote weight
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const name& proxy, const std::vector<name>& producers );

         /**
          * Register proxy action.
          *
          * @details Set `proxy` account as proxy.
          * An account marked as a proxy can vote with the weight of other accounts which
          * have selected it as a proxy. Other accounts must refresh their voteproducer to
          * update the proxy's weight.
          * Storage change is billed to `proxy`.
          *
          * @param rpoxy - the account registering as voter proxy (or unregistering),
          * @param isproxy - if true, proxy is registered; if false, proxy is unregistered.
          *
          * @pre Proxy must have something staked (existing row in voters table)
          * @pre New state must be different than current state
          */
         [[eosio::action]]
         void regproxy( const name& proxy, bool isproxy );

         /**
          * Set the blockchain parameters
          *
          * @details Set the blockchain parameters. By tunning these parameters a degree of
          * customization can be achieved.
          * @param params - New blockchain parameters to set.
          */
         [[eosio::action]]
         void setparams( const eosio::blockchain_parameters& params );

         // functions defined in producer_pay.cpp
         /**
          * Claim rewards action.
          *
          * @details Claim block producing and vote rewards.
          * @param owner - producer account claiming per-block and per-vote rewards.
          */
         [[eosio::action]]
         void claimrewards( const name& owner );

         // functions defined in producer_pay.cpp
         /**
          * To rewards action.
          *
          * @details Transfer to per_block, per_vote and rem savings accounts.
          * @param payer - payer.
          * @param amount - amount of tokens to transfer to per_block, per_vote and rem savings accounts.
          */
         [[eosio::action]]
         void torewards( const name& payer, const asset& amount );

         /**
          * Set privilege status for an account.
          *
          * @details Allows to set privilege status for an account (turn it on/off).
          * @param account - the account to set the privileged status for.
          * @param is_priv - 0 for false, > 0 for true.
          */
         [[eosio::action]]
         void setpriv( const name& account, uint8_t is_priv );

         /**
          * Remove producer action.
          *
          * @details Deactivates a producer by name, if not found asserts.
          * @param producer - the producer account to deactivate.
          */
         [[eosio::action]]
         void rmvproducer( const name& producer );

         /**
          * Update revision action.
          *
          * @details Updates the current revision.
          * @param revision - it has to be incremented by 1 compared with current revision.
          *
          * @pre Current revision can not be higher than 254, and has to be smaller
          * than or equal 1 (“set upper bound to greatest revision supported in the code”).
          */
         [[eosio::action]]
         void updtrevision( uint8_t revision );

         /**
          * Bid name action.
          *
          * @details Allows an account `bidder` to place a bid for a name `newname`.
          * @param bidder - the account placing the bid,
          * @param newname - the name the bid is placed for,
          * @param bid - the amount of system tokens payed for the bid.
          *
          * @pre Bids can be placed only on top-level suffix,
          * @pre Non empty name,
          * @pre Names longer than 12 chars are not allowed,
          * @pre Names equal with 12 chars can be created without placing a bid,
          * @pre Bid has to be bigger than zero,
          * @pre Bid's symbol must be system token,
          * @pre Bidder account has to be different than current highest bidder,
          * @pre Bid must increase current bid by 10%,
          * @pre Auction must still be opened.
          */
         [[eosio::action]]
         void bidname( const name& bidder, const name& newname, const asset& bid );

         /**
          * Bid refund action.
          *
          * @details Allows the account `bidder` to get back the amount it bid so far on a `newname` name.
          *
          * @param bidder - the account that gets refunded,
          * @param newname - the name for which the bid was placed and now it gets refunded for.
          */
         [[eosio::action]]
         void bidrefund( const name& bidder, const name& newname );

         /**
          * Set inflation action.
          *
          * @details Change the annual inflation rate of the core token supply and specify how
          *          the new issued tokens will be distributed based on the following structure.
          *
          *    +----+                          +----------------+
          *    +rate|               +--------->|per vote reward |
          *    +--+-+               |          +----------------+
          *       |            +-----+------+
          *       |     +----->| bp rewards |
          *       v     |      +-----+------+
          *    +-+--+---+-+         |          +----------------+
          *    |new tokens|         +--------->|per block reward|
          *    +----+-----+                    +----------------+
          *             |      +------------+
          *             +----->|  savings   |
          *                    +------------+
          *
          * @param annual_rate - Annual inflation rate of the core token supply.
          *     (eg. For 5% Annual inflation => annual_rate=500
          *          For 1.5% Annual inflation => annual_rate=150
          *
          * @param inflation_pay_factor - Inverse of the fraction of the inflation used to reward block producers.
          *     The remaining inflation will be sent to the `eosio.saving` account.
          *     (eg. For 20% of inflation going to block producer rewards   => inflation_pay_factor = 50000
          *          For 100% of inflation going to block producer rewards  => inflation_pay_factor = 10000).
          *
          * @param votepay_factor - Inverse of the fraction of the block producer rewards to be distributed proportional to blocks produced.
          *     The remaining rewards will be distributed proportional to votes received.
          *     (eg. For 25% of block producer rewards going towards block pay => votepay_factor = 40000
          *          For 75% of block producer rewards going towards block pay => votepay_factor = 13333).
          */
         [[eosio::action]]
         void setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor );

         using init_action = eosio::action_wrapper<"init"_n, &system_contract::init>;
         using newaccount_action = eosio::action_wrapper<"newaccount"_n, &system_contract::newaccount>;
         using activate_action = eosio::action_wrapper<"activate"_n, &system_contract::activate>;
         using delegatebw_action = eosio::action_wrapper<"delegatebw"_n, &system_contract::delegatebw>;
         using deposit_action = eosio::action_wrapper<"deposit"_n, &system_contract::deposit>;
         using withdraw_action = eosio::action_wrapper<"withdraw"_n, &system_contract::withdraw>;
         using buyrex_action = eosio::action_wrapper<"buyrex"_n, &system_contract::buyrex>;
         using unstaketorex_action = eosio::action_wrapper<"unstaketorex"_n, &system_contract::unstaketorex>;
         using sellrex_action = eosio::action_wrapper<"sellrex"_n, &system_contract::sellrex>;
         using cnclrexorder_action = eosio::action_wrapper<"cnclrexorder"_n, &system_contract::cnclrexorder>;
         using rentcpu_action = eosio::action_wrapper<"rentcpu"_n, &system_contract::rentcpu>;
         using rentnet_action = eosio::action_wrapper<"rentnet"_n, &system_contract::rentnet>;
         using fundcpuloan_action = eosio::action_wrapper<"fundcpuloan"_n, &system_contract::fundcpuloan>;
         using fundnetloan_action = eosio::action_wrapper<"fundnetloan"_n, &system_contract::fundnetloan>;
         using defcpuloan_action = eosio::action_wrapper<"defcpuloan"_n, &system_contract::defcpuloan>;
         using defnetloan_action = eosio::action_wrapper<"defnetloan"_n, &system_contract::defnetloan>;
         using updaterex_action = eosio::action_wrapper<"updaterex"_n, &system_contract::updaterex>;
         using rexexec_action = eosio::action_wrapper<"rexexec"_n, &system_contract::rexexec>;
         using setrex_action = eosio::action_wrapper<"setrex"_n, &system_contract::setrex>;
         using mvtosavings_action = eosio::action_wrapper<"mvtosavings"_n, &system_contract::mvtosavings>;
         using mvfrsavings_action = eosio::action_wrapper<"mvfrsavings"_n, &system_contract::mvfrsavings>;
         using consolidate_action = eosio::action_wrapper<"consolidate"_n, &system_contract::consolidate>;
         using closerex_action = eosio::action_wrapper<"closerex"_n, &system_contract::closerex>;
         using undelegatebw_action = eosio::action_wrapper<"undelegatebw"_n, &system_contract::undelegatebw>;
         using refund_action = eosio::action_wrapper<"refund"_n, &system_contract::refund>;
         using refundtostake_action = eosio::action_wrapper<"refundtostake"_n, &system_contract::refundtostake>;
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &system_contract::regproducer>;
         using regproducer2_action = eosio::action_wrapper<"regproducer2"_n, &system_contract::regproducer2>;
         using unregprod_action = eosio::action_wrapper<"unregprod"_n, &system_contract::unregprod>;
         using setram_action = eosio::action_wrapper<"setram"_n, &system_contract::setram>;
         using setmin_account_stake_action = eosio::action_wrapper<"setminstake"_n, &system_contract::setminstake>;
         using setramrate_action = eosio::action_wrapper<"setramrate"_n, &system_contract::setramrate>;
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &system_contract::voteproducer>;
         using regproxy_action = eosio::action_wrapper<"regproxy"_n, &system_contract::regproxy>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
         using torewards_action = eosio::action_wrapper<"torewards"_n, &system_contract::torewards>;

         using rmvproducer_action = eosio::action_wrapper<"rmvproducer"_n, &system_contract::rmvproducer>;
         using updtrevision_action = eosio::action_wrapper<"updtrevision"_n, &system_contract::updtrevision>;
         using bidname_action = eosio::action_wrapper<"bidname"_n, &system_contract::bidname>;
         using bidrefund_action = eosio::action_wrapper<"bidrefund"_n, &system_contract::bidrefund>;
         using setpriv_action = eosio::action_wrapper<"setpriv"_n, &system_contract::setpriv>;
         using setalimits_action = eosio::action_wrapper<"setalimits"_n, &system_contract::setalimits>;
         using setparams_action = eosio::action_wrapper<"setparams"_n, &system_contract::setparams>;
         using setrwrdratio_action = eosio::action_wrapper<"setrwrdratio"_n, &system_contract::setrwrdratio>;
         using setlockperiod_action = eosio::action_wrapper<"setlockperiod"_n, &system_contract::setlockperiod>;
         using setunloperiod_action = eosio::action_wrapper<"setunloperiod"_n, &system_contract::setunloperiod>;

         using setgiftcontra_action = eosio::action_wrapper<"setgiftcontra"_n, &system_contract::setgiftcontra>;
         using setgiftiss_action    = eosio::action_wrapper<"setgiftiss"_n,    &system_contract::setgiftiss>;
         using setgiftattr_action   = eosio::action_wrapper<"setgiftattr"_n,   &system_contract::setgiftattr>;
         using setinflation_action = eosio::action_wrapper<"setinflation"_n, &system_contract::setinflation>;

      private:
         // Implementation details:

         // defined in rem.system.cpp
         static eosio_global_state get_default_parameters();
         static eosio_global_state4 get_default_inflation_parameters();
         static eosio_global_rem_state get_default_rem_parameters();
         uint64_t get_min_threshold_stake();
         symbol core_symbol()const;
         void update_ram_supply();

         // defined in rex.cpp
         void runrex( uint16_t max );
         void update_resource_limits( const name& from, const name& receiver, int64_t delta_net, int64_t delta_cpu );
         void check_voting_requirement( const name& owner,
                                        const char* error_msg = "must vote for at least 21 producers or for a proxy before buying REX" )const;
         rex_order_outcome fill_rex_order( const rex_balance_table::const_iterator& bitr, const asset& rex );
         asset update_rex_account( const name& owner, const asset& proceeds, const asset& unstake_quant, bool force_vote_update = false );
         void channel_to_rex( const name& from, const asset& amount );
         void channel_namebid_to_rex( const int64_t highest_bid );
         template <typename T>
         int64_t rent_rex( T& table, const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );
         template <typename T>
         void fund_rex_loan( T& table, const name& from, uint64_t loan_num, const asset& payment );
         template <typename T>
         void defund_rex_loan( T& table, const name& from, uint64_t loan_num, const asset& amount );
         void transfer_from_fund( const name& owner, const asset& amount );
         void transfer_to_fund( const name& owner, const asset& amount );
         bool rex_loans_available()const;
         bool rex_system_initialized()const { return _rexpool.begin() != _rexpool.end(); }
         bool rex_available()const { return rex_system_initialized() && _rexpool.begin()->total_rex.amount > 0; }
         static time_point_sec get_rex_maturity();
         asset add_to_rex_balance( const name& owner, const asset& payment, const asset& rex_received );
         asset add_to_rex_pool( const asset& payment );
         void process_rex_maturities( const rex_balance_table::const_iterator& bitr );
         void consolidate_rex_balance( const rex_balance_table::const_iterator& bitr,
                                       const asset& rex_in_sell_order );
         int64_t read_rex_savings( const rex_balance_table::const_iterator& bitr );
         void put_rex_savings( const rex_balance_table::const_iterator& bitr, int64_t rex );
         void update_rex_stake( const name& voter );

         void add_loan_to_rex_pool( const asset& payment, int64_t rented_tokens, bool new_loan );
         void remove_loan_from_rex_pool( const rex_loan& loan );
         template <typename Index, typename Iterator>
         int64_t update_renewed_loan( Index& idx, const Iterator& itr, int64_t rented_tokens );
         int64_t get_bancor_output( int64_t inp_reserve, int64_t out_reserve, int64_t inp );

         // defined in delegate_bandwidth.cpp
         void changebw( name from, const name& receiver,
                        const asset& stake_quantity, bool transfer );
         double stake2vote( int64_t staked, time_point locked_stake_period ) const;
         void update_voting_power( const name& voter, const asset& total_update );

         // defined in voting.cpp
         eosio::block_signing_authority convert_to_block_signing_authority( const eosio::public_key& producer_key );
         void register_producer( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location );
         void update_elected_producers( const block_timestamp& timestamp );
         void update_votes( const name& voter, const name& proxy, const std::vector<name>& producers, bool voting );
         void propagate_weight_change( const voter_info& voter );

         // defined in producer_pay.cpp
         int64_t share_pervote_reward_between_producers(int64_t amount);
         void update_pervote_shares();
         void update_standby();

         int64_t share_perstake_reward_between_guardians(int64_t amount);

         void claim_perstake( const name& voter );
         void claim_pervote( const name& prod );

         // defined in rem.system.cpp
         // to keep Guardian status, account should reassert its vote every eosio_global_rem_state::reassertion_period
         bool vote_is_reasserted( eosio::time_point last_reassertion_time ) const;

         // calculates amount of free bytes for current stake
         int64_t ram_gift_bytes( int64_t stake ) const;

         //defined in rotation.cpp
         std::vector<eosio::producer_authority> get_rotated_schedule();

         template <auto system_contract::*...Ptrs>
         class registration {
            public:
               template <auto system_contract::*P, auto system_contract::*...Ps>
               struct for_each {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, args... );
                     for_each<Ps...>::call( this_contract, std::forward<Args>(args)... );
                  }
               };
               template <auto system_contract::*P>
               struct for_each<P> {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, std::forward<Args>(args)... );
                  }
               };

               template <typename... Args>
               constexpr void operator() ( Args&&... args )
               {
                  for_each<Ptrs...>::call( this_contract, std::forward<Args>(args)... );
               }

               system_contract* this_contract;
         };

         registration<&system_contract::update_rex_stake> vote_stake_updater{ this };
   };

   /** @}*/ // end of @defgroup eosiosystem rem.system
} /// eosiosystem
