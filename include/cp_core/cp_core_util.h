//
// Created by vasilis on 28/06/2021.
//

#ifndef CP_CORE_UTIL_H
#define CP_CORE_UTIL_H

#include <cp_core_generic_util.h>

/*--------------------------------------------------------------------------
 * --------------------KVS_FILTERING--RECEIVING PROPOSE ACCEPT---------------
 * --------------------------------------------------------------------------*/

// Check the global RMW-id structure, to see if an RMW has already been committed
static inline bool the_rmw_has_committed(mica_op_t *kv_ptr,
                                         uint64_t rmw_id,
                                         uint32_t log_no, uint16_t t_id,
                                         struct rmw_rep_last_committed *rep)
{
  uint64_t glob_sess_id = rmw_id % GLOBAL_SESSION_NUM;
  check_the_rmw_has_committed(glob_sess_id);

  if (committed_glob_sess_rmw_id[glob_sess_id] >= rmw_id) {
    bool same_log = kv_ptr->last_committed_log_no == log_no;
    rep->opcode = (uint8_t) (same_log ? RMW_ID_COMMITTED_SAME_LOG : RMW_ID_COMMITTED);
    check_when_rmw_has_committed(kv_ptr, rep, glob_sess_id, log_no, rmw_id, t_id);
    return true;
  }
  else return false;
}

static inline void log_no_too_low(uint32_t log_no, mica_op_t *kv_ptr,
                                  uint64_t rmw_l_id,
                                  uint16_t t_id,
                                  cp_rmw_rep_t *rep)
{
  print_log_too_small(log_no, kv_ptr, rmw_l_id, t_id);
  rep->opcode = LOG_TOO_SMALL;
  fill_reply_entry_with_committed_RMW (kv_ptr, rep, t_id);
}

static inline void log_no_too_high(uint32_t log_no, mica_op_t *kv_ptr,
                                  uint16_t t_id,
                                  cp_rmw_rep_t *rep)
{
  print_is_log_too_high(log_no, kv_ptr, t_id);
  rep->opcode = LOG_TOO_HIGH;
}

static inline bool is_log_too_low_or_too_high(uint32_t log_no, mica_op_t *kv_ptr,
                                              uint64_t rmw_l_id,
                                              uint16_t t_id,
                                              cp_rmw_rep_t *rep)
{
  uint32_t expected_log_no = kv_ptr->last_committed_log_no + 1;
  if (log_no == expected_log_no)
    return false;
  else if (log_no < expected_log_no)
    log_no_too_low(log_no, kv_ptr, rmw_l_id, t_id, rep);
  else
    log_no_too_high(log_no, kv_ptr, t_id, rep);

  return true;
}


static inline bool is_log_lower_higher_or_has_rmw_committed(uint32_t log_no, mica_op_t *kv_ptr,
                                                           uint64_t rmw_l_id,
                                                           uint16_t t_id,
                                                           cp_rmw_rep_t *rep)
{
  check_log_nos_of_kv_ptr(kv_ptr, "is_log_smaller_or_has_rmw_committed", t_id);

  if (the_rmw_has_committed(kv_ptr, rmw_l_id, log_no, t_id, rep))
    return true;
  return is_log_too_low_or_too_high(log_no, kv_ptr, rmw_l_id, t_id, rep);
}


static inline uint8_t is_base_ts_too_small(mica_op_t *kv_ptr,
                                           struct propose *prop,
                                           struct rmw_rep_last_committed *rep,
                                           uint16_t t_id)
{
  if (prop->base_ts.version == DO_NOT_CHECK_BASE_TS) return RMW_ACK;

  compare_t  comp_ts = compare_ts(&kv_ptr->ts, &prop->base_ts);
  if (comp_ts == GREATER) {
    rep->ts.version = kv_ptr->ts.version;
    rep->ts.m_id = kv_ptr->ts.m_id;
    memcpy(rep->value, kv_ptr->value, (size_t) RMW_VALUE_SIZE);
    return RMW_ACK_BASE_TS_STALE;
  }
  else return RMW_ACK;
}



// Search in the prepare entries for an lid (used when receiving a prep reply)
static inline int search_prop_entries_with_l_id(loc_entry_t * loc_entry_array,
                                                uint8_t state,
                                                uint64_t l_id)
{
  uint16_t entry = (uint16_t) (l_id % SESSIONS_PER_THREAD);
  check_search_prop_entries_with_l_id(entry);
  if (loc_entry_array[entry].state == state &&
      loc_entry_array[entry].l_id == l_id)
    return entry;
  return -1; // i.e. l_id not found!!

}



/*--------------------------------------------------------------------------
 * --------------------RMW-INIT---------------------------------------------
 * --------------------------------------------------------------------------*/

// If a local RMW managed to grab a kv_ptr, then it sets up its local entry
static inline void fill_loc_rmw_entry_on_grabbing_kv_ptr(loc_entry_t *loc_entry,
                                                         uint32_t version, uint8_t state,
                                                         uint16_t sess_i, uint16_t t_id)
{
  check_when_filling_loc_entry(loc_entry, sess_i, version, t_id);
  local_rmw_ack(loc_entry);
  loc_entry->state = state;
  loc_entry->new_ts.version = version;
  loc_entry->new_ts.m_id = (uint8_t) machine_id;
}

// Initialize a local  RMW entry on the first time it gets allocated
static inline void init_loc_entry(trace_op_t *op,
                                  uint16_t t_id, loc_entry_t* loc_entry)
{
  check_when_init_loc_entry(loc_entry, op);
  loc_entry->opcode = op->opcode;
  if (opcode_is_compare_rmw(op->opcode) || op->opcode == RMW_PLAIN_WRITE)
    memcpy(loc_entry->value_to_write, op->value_to_write, op->real_val_len);
  loc_entry->killable = op->opcode == COMPARE_AND_SWAP_WEAK;
  if (opcode_is_compare_rmw(op->opcode))
    loc_entry->compare_val = op->value_to_read; //expected value
  else if (op->opcode == FETCH_AND_ADD) {
    loc_entry->compare_val = op->value_to_write; // value to be added
  }
  loc_entry->fp_detected = false;
  loc_entry->rmw_val_len = op->real_val_len;
  loc_entry->rmw_is_successful = false;
  loc_entry->all_aboard = ENABLE_ALL_ABOARD && op->attempt_all_aboard;
  loc_entry->avoid_val_in_com = false;
  loc_entry->base_ts_found = false;
  loc_entry->all_aboard_time_out = 0;
  memcpy(&loc_entry->key, &op->key, KEY_SIZE);
  memset(&loc_entry->rmw_reps, 0, sizeof(struct rmw_rep_info));
  loc_entry->index_to_req_array = op->index_to_req_array;

  loc_entry->back_off_cntr = 0;
  loc_entry->log_too_high_cntr = 0;
  loc_entry->helping_flag = NOT_HELPING;
  loc_entry->rmw_id.id+= GLOBAL_SESSION_NUM;
  advance_loc_entry_l_id(loc_entry, t_id);
  loc_entry->accepted_log_no = 0;
  loc_entry->help_loc_entry->state = INVALID_RMW;
  check_loc_entry_init_rmw_id(loc_entry, t_id);
}


// Activate the entry that belongs to a given key to initiate an RMW (either a local or a remote)
static inline void activate_kv_pair(uint8_t state, uint32_t new_version, mica_op_t *kv_ptr,
                                    uint8_t opcode, uint8_t new_ts_m_id, loc_entry_t *loc_entry,
                                    uint64_t rmw_id,
                                    uint32_t log_no, uint16_t t_id, const char *message)
{
  check_activate_kv_pair(state, kv_ptr, log_no, message);
  // pass the new base_ts!
  kv_ptr->opcode = opcode;
  kv_ptr->prop_ts.m_id = new_ts_m_id;
  kv_ptr->prop_ts.version = new_version;
  kv_ptr->rmw_id.id = rmw_id;
  kv_ptr->state = state;
  kv_ptr->log_no = log_no;

  if (state == ACCEPTED) {
    check_activate_kv_pair_accepted(kv_ptr, new_version, new_ts_m_id);
    kv_ptr->accepted_ts = kv_ptr->prop_ts;
    kv_ptr->accepted_log_no = log_no;
    if (loc_entry != NULL && loc_entry->all_aboard) {
      perform_the_rmw_on_the_loc_entry(kv_ptr, loc_entry, t_id);
    }
  }
  check_after_activate_kv_pair(kv_ptr, message, state, t_id);
}


/*--------------------------------------------------------------------------
 * --------------------REMOTE RMW REQUESTS-------------------------------------
 * --------------------------------------------------------------------------*/

static inline uint8_t tell_prop_to_help_lower_acc(cp_prop_t *prop,
                                                  mica_op_t *kv_ptr,
                                                  cp_rmw_rep_t *rep)
{
  assign_ts_to_netw_ts(&rep->ts, &kv_ptr->accepted_ts);
  rep->rmw_id = kv_ptr->rmw_id.id;
  memcpy(rep->value, kv_ptr->last_accepted_value, (size_t) RMW_VALUE_SIZE);
  rep->log_no_or_base_version = kv_ptr->base_acc_ts.version;
  rep->base_m_id = kv_ptr->base_acc_ts.m_id;
  return  SEEN_LOWER_ACC;
}


static inline uint8_t compare_kv_ptr_acc_ts_with_prop_ts(cp_prop_t *prop,
                                                         mica_op_t *kv_ptr,
                                                         cp_rmw_rep_t *rep)
{
  compare_t acc_ts_comp = compare_netw_ts_with_ts(&prop->ts, &kv_ptr->accepted_ts);
  if (kv_ptr->state == ACCEPTED && acc_ts_comp == GREATER) {
    return kv_ptr->rmw_id.id == prop->t_rmw_id ?
           RMW_ACK_ACC_SAME_RMW :
           tell_prop_to_help_lower_acc(prop, kv_ptr, rep);
  }
  else  return RMW_ACK;
}

// Look at the kv_ptr to answer to a propose message-- kv pair lock is held when calling this
static inline uint8_t propose_snoops_entry(cp_prop_t *prop,
                                           mica_op_t *kv_ptr,
                                           uint8_t m_id,
                                           uint16_t t_id,
                                           cp_rmw_rep_t *rep)
{
  check_propose_snoops_entry(prop, kv_ptr);
  uint8_t return_flag;
  compare_t prop_ts_comp = compare_netw_ts_with_ts(&prop->ts, &kv_ptr->prop_ts);

  if (prop_ts_comp == GREATER) {
    assign_netw_ts_to_ts(&kv_ptr->prop_ts, &prop->ts);
    return_flag = compare_kv_ptr_acc_ts_with_prop_ts(prop, kv_ptr, rep);
  }
  else {
    assign_ts_to_netw_ts(&rep->ts, &kv_ptr->prop_ts);
    return_flag = SEEN_HIGHER_PROP;
  }

  check_state_with_allowed_flags(5, return_flag, RMW_ACK, RMW_ACK_ACC_SAME_RMW,
                                 SEEN_HIGHER_PROP, SEEN_LOWER_ACC);
  return return_flag;
}

static inline uint8_t ack_acc_if_ts_equal_greater(cp_acc_t *acc,
                                               mica_op_t *kv_ptr,
                                               compare_t ts_comp,
                                               uint16_t t_id)
{
  print_accept_snoops_entry(acc, kv_ptr, ts_comp, t_id);
  return RMW_ACK;
}

static inline uint8_t nack_acc_if_ts_too_low(mica_op_t *kv_ptr,
                                             cp_rmw_rep_t *rep){
  check_state_with_allowed_flags(3, kv_ptr->state,
                                 PROPOSED, ACCEPTED);
  assign_ts_to_netw_ts(&rep->ts, &kv_ptr->prop_ts);
  return kv_ptr->state == PROPOSED ?
         SEEN_HIGHER_PROP : SEEN_HIGHER_ACC;

}

static uint8_t acc_inspects_kv_ts(cp_acc_t *acc,
                                  mica_op_t *kv_ptr,
                                  cp_rmw_rep_t *rep,
                                  uint16_t t_id)
{
  // Higher Ts  = Success,  Lower Ts  = Failure
  compare_t ts_comp = compare_netw_ts_with_ts(&acc->ts, &kv_ptr->prop_ts);
  // Higher Ts  = Success
  if (ts_comp == EQUAL || ts_comp == GREATER)
    return ack_acc_if_ts_equal_greater(acc, kv_ptr, ts_comp, t_id);
  else if (ts_comp == SMALLER)
    return nack_acc_if_ts_too_low(kv_ptr, rep);
  else my_assert(false, "");
}

// Look at an RMW entry to answer to an accept message-- kv pair lock is held when calling this
static inline uint8_t accept_snoops_entry(cp_acc_t *acc, mica_op_t *kv_ptr, uint8_t sender_m_id,
                                          uint16_t t_id, cp_rmw_rep_t *rep)
{
  check_accept_snoops_entry(acc, kv_ptr);
  uint8_t return_flag =  kv_ptr->state == INVALID_RMW ?
      RMW_ACK : acc_inspects_kv_ts(acc, kv_ptr, rep, t_id);

  print_check_after_accept_snoops_entry(acc, kv_ptr, rep, return_flag, t_id);
  return return_flag;
}

//Handle a remote propose/accept whose log number is big enough
static inline uint8_t handle_remote_prop_or_acc_in_kvs(mica_op_t *kv_ptr, void *prop_or_acc,
                                                       uint8_t sender_m_id, uint16_t t_id,
                                                       struct rmw_rep_last_committed *rep, uint32_t log_no,
                                                       bool is_prop)
{
  // if the log number is higher than expected blindly ack
  if (log_no > kv_ptr->log_no) {
    check_log_no_on_ack_remote_prop_acc(kv_ptr, log_no);
    return RMW_ACK;
  }
  else
    return is_prop ? propose_snoops_entry((cp_prop_t *) prop_or_acc, kv_ptr, sender_m_id, t_id, rep) :
           accept_snoops_entry((cp_acc_t *) prop_or_acc, kv_ptr, sender_m_id, t_id, rep);
}


/*--------------------------------------------------------------------------
 * --------------------ACCEPTING-------------------------------------
 * --------------------------------------------------------------------------*/

static inline void bookkeeping_if_creating_prop_ack(cp_prop_t *prop,
                                                    cp_rmw_rep_t *prop_rep,
                                                    mica_op_t *kv_ptr,
                                                    uint16_t t_id)
{
  // if the propose is going to be acked record its information in the kv_ptr
  if (prop_rep->opcode == RMW_ACK) {
    if (ENABLE_ASSERTIONS) assert(prop->log_no >= kv_ptr->log_no);
    activate_kv_pair(PROPOSED, prop->ts.version, kv_ptr, prop->opcode,
                     prop->ts.m_id, NULL, prop->t_rmw_id, prop->log_no, t_id,
                     ENABLE_ASSERTIONS ? "received propose" : NULL);
  }
  if (prop_rep->opcode == RMW_ACK || prop_rep->opcode == RMW_ACK_ACC_SAME_RMW) {
    prop_rep->opcode = is_base_ts_too_small(kv_ptr, prop, prop_rep, t_id);
  }
  check_create_prop_rep(prop, kv_ptr);
}

static inline void create_prop_rep_after_locking_kv_ptr(cp_prop_t *prop,
                                                        cp_prop_mes_t *prop_mes,
                                                        cp_rmw_rep_t *prop_rep,
                                                        mica_op_t *kv_ptr,
                                                        uint64_t *number_of_reqs,
                                                        uint16_t t_id)
{
  if (!is_log_lower_higher_or_has_rmw_committed(prop->log_no, kv_ptr,
                                                prop->t_rmw_id,
                                                t_id, prop_rep)) {
    prop_rep->opcode = handle_remote_prop_or_acc_in_kvs(kv_ptr, (void *) prop, prop_mes->m_id, t_id,
                                                        prop_rep, prop->log_no, true);
    bookkeeping_if_creating_prop_ack(prop, prop_rep, kv_ptr, t_id);
  }
  dbg_kv_ptr_create_acc_prop_rep(kv_ptr, number_of_reqs);
}

static inline void create_prop_rep(cp_prop_t *prop,
                                   cp_prop_mes_t *prop_mes,
                                   cp_rmw_rep_t *prop_rep,
                                   mica_op_t *kv_ptr,
                                   uint16_t t_id)
{
  uint64_t number_of_reqs = 0;

  prop_rep->l_id = prop->l_id;
  lock_kv_ptr(kv_ptr, t_id);
  {
    create_prop_rep_after_locking_kv_ptr(prop, prop_mes, prop_rep, kv_ptr, &number_of_reqs, t_id);
  }
  unlock_kv_ptr(kv_ptr, t_id);
  print_log_on_rmw_recv(prop->t_rmw_id, prop_mes->m_id, prop->log_no, prop_rep,
                        prop->ts, kv_ptr, number_of_reqs, false, t_id);
}

static inline void create_acc_rep_after_locking_kv_ptr(cp_acc_t *acc,
                                                       cp_acc_mes_t *acc_mes,
                                                       cp_rmw_rep_t *acc_rep,
                                                       mica_op_t *kv_ptr,
                                                       uint64_t *number_of_reqs,
                                                       uint16_t t_id)
{
  uint64_t rmw_l_id = acc->t_rmw_id;
  //my_printf(cyan, "Received accept with rmw_id %u, glob_sess %u \n", rmw_l_id, glob_sess_id);
  uint32_t log_no = acc->log_no;
  uint8_t acc_m_id = acc_mes->m_id;
  if (!is_log_lower_higher_or_has_rmw_committed(log_no, kv_ptr, rmw_l_id, t_id, acc_rep)) {
    acc_rep->opcode = handle_remote_prop_or_acc_in_kvs(kv_ptr, (void *) acc, acc_m_id, t_id, acc_rep, log_no, false);
    if (acc_rep->opcode == RMW_ACK) {
      activate_kv_pair(ACCEPTED, acc->ts.version, kv_ptr, acc->opcode,
                       acc->ts.m_id, NULL, rmw_l_id, log_no, t_id,
                       ENABLE_ASSERTIONS ? "received accept" : NULL);
      memcpy(kv_ptr->last_accepted_value, acc->value, (size_t) RMW_VALUE_SIZE);
      kv_ptr->base_acc_ts = acc->base_ts;
    }
  }
  dbg_kv_ptr_create_acc_prop_rep(kv_ptr, number_of_reqs);
}

static inline void create_acc_rep(cp_acc_t *acc,
                                  cp_acc_mes_t *acc_mes,
                                  cp_rmw_rep_t *acc_rep,
                                  mica_op_t *kv_ptr,
                                  uint16_t t_id)
{
  uint64_t number_of_reqs = 0;
  acc_rep->l_id = acc->l_id;
  lock_kv_ptr(kv_ptr, t_id);
  {
    create_acc_rep_after_locking_kv_ptr(acc, acc_mes, acc_rep, kv_ptr, &number_of_reqs, t_id);
  }
  unlock_kv_ptr(kv_ptr, t_id);
  print_log_on_rmw_recv(acc->t_rmw_id, acc_mes->m_id, acc->log_no, acc_rep,
                        acc->ts, kv_ptr, number_of_reqs, true, t_id);
}

/*--------------------------------------------------------------------------
 * --------------------RECEIVING REPLY-UTILITY-------------------------------------
 * --------------------------------------------------------------------------*/

static inline void take_kv_ptr_to_acc_state(mica_op_t *kv_ptr,
                                            loc_entry_t *loc_entry,
                                            uint16_t t_id)
{
  checks_before_local_accept(kv_ptr, loc_entry, t_id);

  kv_ptr->state = ACCEPTED;
  // calculate the new value depending on the type of RMW
  perform_the_rmw_on_the_loc_entry(kv_ptr, loc_entry, t_id);
  //when last_accepted_value is update also update the acc_base_ts
  kv_ptr->base_acc_ts = kv_ptr->ts;
  kv_ptr->accepted_ts = loc_entry->new_ts;
  kv_ptr->accepted_log_no = kv_ptr->log_no;
  checks_after_local_accept(kv_ptr, loc_entry, t_id);
}

// After gathering a quorum of proposal acks, check if you can accept locally-- THIS IS STRICTLY LOCAL RMWS -- no helps
// Every RMW that gets committed must pass through this function successfully (at least one time)
static inline void attempt_local_accept(loc_entry_t *loc_entry,
                                        uint16_t t_id)
{
  mica_op_t *kv_ptr = loc_entry->kv_ptr;
  checks_preliminary_local_accept(kv_ptr, loc_entry, t_id);

  lock_kv_ptr(loc_entry->kv_ptr, t_id);

  if (if_already_committed_bcast_commits(loc_entry, t_id)) {
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);
    return;
  }

  if (same_rmw_id_same_ts_and_invalid(kv_ptr, loc_entry)) {
    take_kv_ptr_to_acc_state(kv_ptr, loc_entry, t_id);
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);
    loc_entry->state = ACCEPTED;
  }
  else { // the entry stores a different rmw_id and thus our proposal has been won by another
    // Some other RMW has won the kv_ptr we are trying to get accepted
    checks_after_failure_to_locally_accept(kv_ptr, loc_entry, t_id);
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);
    loc_entry->state = NEEDS_KV_PTR;
  }
}

static inline void take_local_kv_ptr_to_acc_state_when_helping(mica_op_t *kv_ptr,
                                                               loc_entry_t *loc_entry,
                                                               loc_entry_t* help_loc_entry,
                                                               uint16_t t_id)
{
  kv_ptr->state = ACCEPTED;
  kv_ptr->rmw_id = help_loc_entry->rmw_id;
  kv_ptr->accepted_ts = help_loc_entry->new_ts;
  kv_ptr->accepted_log_no = kv_ptr->log_no;
  write_kv_ptr_acc_val(kv_ptr, help_loc_entry->value_to_write, (size_t) RMW_VALUE_SIZE);
  kv_ptr->base_acc_ts = help_loc_entry->base_ts;// the base_ts of the RMW we are helping
  checks_after_local_accept_help(kv_ptr, loc_entry, t_id);
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);
  loc_entry->state = ACCEPTED;
}

static inline void clean_up_if_cannot_accept_locally(mica_op_t *kv_ptr,
                                                     loc_entry_t *loc_entry,
                                                     uint16_t t_id)
{
  checks_after_failure_to_locally_accept_help(kv_ptr, loc_entry, t_id);
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);
  loc_entry->state = NEEDS_KV_PTR;
  loc_entry->help_loc_entry->state = INVALID_RMW;
}


// After gathering a quorum of proposal reps, one of them was a lower TS accept, try and help it
static inline void attempt_local_accept_to_help(loc_entry_t *loc_entry,
                                                uint16_t t_id)
{
  mica_op_t *kv_ptr = loc_entry->kv_ptr;
  loc_entry_t* help_loc_entry = loc_entry->help_loc_entry;

  help_loc_entry->new_ts = loc_entry->new_ts;
  checks_preliminary_local_accept_help(kv_ptr, loc_entry, help_loc_entry);

  lock_kv_ptr(loc_entry->kv_ptr, t_id);
  comment_on_why_we_dont_check_if_rmw_committed();

  bool can_accept_locally = find_out_if_can_accept_help_locally(kv_ptr, loc_entry,
                                                                help_loc_entry, t_id);
  if (can_accept_locally)
    take_local_kv_ptr_to_acc_state_when_helping(kv_ptr, loc_entry,
                                                help_loc_entry, t_id);
  else clean_up_if_cannot_accept_locally(kv_ptr, loc_entry, t_id);

}


/*--------------------------------------------------------------------------
 * --------------------COMMITING-------------------------------------
 * --------------------------------------------------------------------------*/

// Check if it's a commit without a value -- if it cannot be committed
// then do not attempt to overwrite the value and timestamp, because the commit's
// value and ts are stored in the kv_ptr->accepted_value/ts and may have been lost
static inline void handle_commit_with_no_val (mica_op_t *kv_ptr,
                                              commit_info_t *com_info,
                                              uint16_t t_id)
{
  if (com_info->no_value) {
    if (!can_process_com_no_value(kv_ptr, com_info, t_id)) {
      com_info->overwrite_kv = false;
    }
  }
}

static inline void clear_kv_state_advance_log_no (mica_op_t *kv_ptr,
                                                  commit_info_t *com_info)
{
  if (kv_ptr->log_no <= com_info->log_no) {
    kv_ptr->log_no = com_info->log_no;
    kv_ptr->state = INVALID_RMW;
  }
}

static inline void apply_val_if_carts_bigger(mica_op_t *kv_ptr,
                                             commit_info_t *com_info,
                                             uint16_t t_id)
{
  if (com_info->overwrite_kv) {
    compare_t cart_comp = compare_carts(&com_info->base_ts, com_info->log_no,
                                        &kv_ptr->ts, kv_ptr->last_committed_log_no);
    check_on_overwriting_commit_algorithm(kv_ptr, com_info, cart_comp, t_id);
    if (cart_comp == GREATER) {
      write_kv_ptr_val(kv_ptr, com_info->value, (size_t) VALUE_SIZE, com_info->flag);
      kv_ptr->ts = com_info->base_ts;
    }
  }
}

static inline void advance_last_comm_log_no_and_rmw_id(mica_op_t *kv_ptr,
                                                       commit_info_t *com_info,
                                                       uint16_t t_id)
{
  check_on_updating_rmw_meta_commit_algorithm(kv_ptr, com_info, t_id);
  if (kv_ptr->last_committed_log_no < com_info->log_no) {
    kv_ptr->last_committed_log_no = com_info->log_no;
    kv_ptr->last_committed_rmw_id = com_info->rmw_id;
  }
}

static inline void register_commit(mica_op_t *kv_ptr,
                                   commit_info_t *com_info,
                                   uint16_t t_id)
{
  register_committed_rmw_id(com_info->rmw_id.id, t_id);

  check_registered_against_kv_ptr_last_committed(kv_ptr, com_info->rmw_id.id,
                                                 com_info->message, t_id);
}

static inline void commit_algorithm(mica_op_t *kv_ptr,
                                    commit_info_t *com_info,
                                    uint16_t t_id)
{
  check_inputs_commit_algorithm(kv_ptr, com_info, t_id);

  lock_kv_ptr(kv_ptr, t_id); {
    check_state_before_commit_algorithm(kv_ptr, com_info, t_id);
    handle_commit_with_no_val(kv_ptr, com_info, t_id);
    clear_kv_state_advance_log_no(kv_ptr, com_info);
    apply_val_if_carts_bigger(kv_ptr, com_info, t_id);
    advance_last_comm_log_no_and_rmw_id(kv_ptr, com_info, t_id);
    register_commit(kv_ptr, com_info, t_id);
  }
  unlock_kv_ptr(kv_ptr, t_id);
}

static inline void fil_commit_info_based_on_flag(void* rmw, loc_entry_t *loc_entry,
                                                 commit_info_t *com_info,
                                                 uint8_t flag, uint16_t t_id)
{
  switch (flag) {
    case FROM_LOG_TOO_LOW_REP:
      fill_commit_info_from_rep(com_info, rmw, flag);
      break;
    case FROM_ALREADY_COMM_REP:
    case FROM_LOCAL:
      fill_commit_info_from_local(com_info, loc_entry, flag);
      break;
    case FROM_ALREADY_COMM_REP_HELP:
    case FROM_LOCAL_HELP:
      fill_commit_info_from_loc_help(com_info, loc_entry->help_loc_entry, flag);
      break;
    case FROM_REMOTE_COMMIT:
      fill_commit_info_from_rem_commit(com_info, rmw, flag);
      break;
    case FROM_REMOTE_COMMIT_NO_VAL:
      fill_commit_info_from_rem_commit_no_val(com_info, rmw, flag);
      break;
    default: my_assert(false, "");
  }
}

static inline void commit_rmw(mica_op_t *kv_ptr,
                              void* rmw, loc_entry_t *loc_entry,
                              uint8_t flag, uint16_t t_id)
{

  process_commit_flags(rmw, loc_entry, &flag);
  commit_info_t com_info;
  fil_commit_info_based_on_flag(rmw, loc_entry, &com_info, flag, t_id);
  commit_algorithm(kv_ptr, &com_info, t_id);
}


static inline void free_session_and_reinstate_loc_entry(sess_stall_t *stall_info,
                                                        loc_entry_t *loc_entry,
                                                        uint16_t t_id)
{
  switch(loc_entry->helping_flag)
  {
    case NOT_HELPING:
    case PROPOSE_NOT_LOCALLY_ACKED:
    case PROPOSE_LOCALLY_ACCEPTED:
      loc_entry->state = INVALID_RMW;
      free_session_from_rmw(loc_entry, stall_info, true, t_id);
      break;
    case HELPING:
      reinstate_loc_entry_after_helping(loc_entry, t_id);
      break;
    case HELP_PREV_COMMITTED_LOG_TOO_HIGH:
      loc_entry->state = RETRY_WITH_BIGGER_TS;
      loc_entry->helping_flag = NOT_HELPING;
      break;
    default: my_assert(false, "");
  }
}

// On gathering quorum of acks for commit, commit locally and signal that the session must be freed if not helping
static inline void act_on_quorum_of_commit_acks(sess_stall_t *stall_info,
                                                loc_entry_t *loc_entry,
                                                uint16_t t_id)
{
  check_act_on_quorum_of_commit_acks(loc_entry);

  if (loc_entry->helping_flag != HELP_PREV_COMMITTED_LOG_TOO_HIGH)
    commit_rmw(loc_entry->kv_ptr, NULL, loc_entry, FROM_LOCAL, t_id);

  free_session_and_reinstate_loc_entry(stall_info, loc_entry,t_id);
}



/*--------------------------------------------------------------------------
 * --------------------HANDLE REPLIES-------------------------------------
 * --------------------------------------------------------------------------*/

static inline void overwrite_help_loc_entry(loc_entry_t* loc_entry,
                                            cp_rmw_rep_t* prop_rep)
{
  loc_entry_t *help_loc_entry = loc_entry->help_loc_entry;
  if (loc_entry->helping_flag == PROPOSE_LOCALLY_ACCEPTED) loc_entry->helping_flag = NOT_HELPING;
  assign_netw_ts_to_ts(&help_loc_entry->new_ts, &prop_rep->ts);
  help_loc_entry->base_ts.version = prop_rep->log_no_or_base_version;
  help_loc_entry->base_ts.m_id = prop_rep->base_m_id;
  help_loc_entry->log_no = loc_entry->log_no;
  help_loc_entry->state = ACCEPTED;
  help_loc_entry->rmw_id.id = prop_rep->rmw_id;
  memcpy(help_loc_entry->value_to_write, prop_rep->value, (size_t) RMW_VALUE_SIZE);
  help_loc_entry->key = loc_entry->key;
}


static inline void store_rmw_rep_in_help_loc_entry(loc_entry_t* loc_entry,
                                                   cp_rmw_rep_t* prop_rep,
                                                   uint16_t t_id)
{
  loc_entry_t *help_loc_entry = loc_entry->help_loc_entry;
  compare_t ts_comp = compare_netw_ts_with_ts(&prop_rep->ts, &help_loc_entry->new_ts);
  check_store_rmw_rep_to_help_loc_entry(loc_entry, prop_rep, ts_comp);

  bool have_not_found_any_ts_to_help = help_loc_entry->state == INVALID_RMW;
  bool found_higher_ts_to_help = ts_comp == GREATER;


  bool must_overwrite_help_loc_entry = have_not_found_any_ts_to_help ||
                                       found_higher_ts_to_help;

  if (must_overwrite_help_loc_entry)
    overwrite_help_loc_entry(loc_entry, prop_rep);
}

static inline void bookkeeping_for_rep_info(rmw_rep_info_t *rep_info,
                                            cp_rmw_rep_t *rep)
{
  rep_info->tot_replies++;
  if (rep_info->tot_replies >= QUORUM_NUM) {
    rep_info->ready_to_inspect = true;
  }
  if (rep->opcode > RMW_ACK_BASE_TS_STALE) rep_info->nacks++;
}

static inline void handle_rmw_rep_ack(cp_rmw_rep_mes_t *rep_mes,
                                      cp_rmw_rep_t *rep,
                                      loc_entry_t *loc_entry,
                                      rmw_rep_info_t *rep_info,
                                      bool is_accept,
                                      const uint16_t t_id)
{
  if (rep->opcode == RMW_ACK_BASE_TS_STALE) {
    write_kv_if_conditional_on_netw_ts(loc_entry->kv_ptr, rep->value,
                                       (size_t) VALUE_SIZE, FROM_BASE_TS_STALE, rep->ts);
    assign_netw_ts_to_ts(&loc_entry->base_ts, &rep->ts);
  }
  loc_entry->base_ts_found = true;
  rep_info->acks++;
  check_handle_prop_or_acc_rep_ack(rep_mes, rep_info, is_accept, t_id);
}

static inline void handle_rmw_rep_rmw_id_committed(loc_entry_t *loc_entry,
                                                   cp_rmw_rep_t *rep,
                                                   rmw_rep_info_t *rep_info,
                                                   const uint16_t t_id)
{
  if (rep->opcode == RMW_ID_COMMITTED)
    rep_info->no_need_to_bcast = true;
  rep_info->ready_to_inspect = true;
  rep_info->rmw_id_commited++;
  commit_rmw(loc_entry->kv_ptr, NULL, loc_entry,
             FROM_ALREADY_COMM_REP, t_id);
}

static inline void handle_rmw_rep_log_too_small(loc_entry_t *loc_entry,
                                                cp_rmw_rep_t *rep,
                                                rmw_rep_info_t *rep_info,
                                                const uint16_t t_id)
{
  rep_info->ready_to_inspect = true;
  rep_info->log_too_small++;
  commit_rmw(loc_entry->kv_ptr, (void*) rep, loc_entry,
             FROM_LOG_TOO_LOW_REP, t_id);
}

static inline void handle_rmw_rep_seen_lower_acc(loc_entry_t *loc_entry,
                                                 cp_rmw_rep_t *rep,
                                                 rmw_rep_info_t *rep_info,
                                                 bool is_accept,
                                                 const uint16_t t_id)
{
  rep_info->already_accepted++;
  check_handle_rmw_rep_seen_lower_acc(loc_entry, rep, is_accept);

  bool no_higher_priority_reps_have_been_seen =
      rep_info->seen_higher_prop_acc +
      rep_info->rmw_id_commited + rep_info->log_too_small == 0;

  if (no_higher_priority_reps_have_been_seen)
    store_rmw_rep_in_help_loc_entry(loc_entry, rep, t_id);

}

static inline void handle_rmw_rep_higher_acc_prop(cp_rmw_rep_t *rep,
                                                  rmw_rep_info_t *rep_info,
                                                  bool is_accept,
                                                  const uint16_t t_id)
{
  if (!is_accept) rep_info->ready_to_inspect = true;
  rep_info->seen_higher_prop_acc++;
  print_handle_rmw_rep_seen_higher(rep, rep_info, is_accept, t_id);
  if (rep->ts.version > rep_info->seen_higher_prop_version) {
    rep_info->seen_higher_prop_version = rep->ts.version;
    print_handle_rmw_rep_higher_ts(rep_info, t_id);
  }
}

static inline void handle_rmw_rep_log_too_high(rmw_rep_info_t *rep_info)
{
  rep_info->log_too_high++;
}

static inline void handle_rmw_rep_based_on_opcode(cp_rmw_rep_mes_t *rep_mes,
                                                  loc_entry_t *loc_entry,
                                                  cp_rmw_rep_t *rep,
                                                  rmw_rep_info_t *rep_info,
                                                  bool is_accept,
                                                  const uint16_t t_id)
{
  switch (rep->opcode) {
    case RMW_ACK_BASE_TS_STALE:
    case RMW_ACK:
      handle_rmw_rep_ack(rep_mes, rep, loc_entry,
                         rep_info, is_accept, t_id);
      break;
    case RMW_ID_COMMITTED:
    case RMW_ID_COMMITTED_SAME_LOG:
      handle_rmw_rep_rmw_id_committed(loc_entry, rep, rep_info, t_id);
      break;
    case LOG_TOO_SMALL:
      handle_rmw_rep_log_too_small(loc_entry, rep, rep_info, t_id);
      break;
    case SEEN_LOWER_ACC:
      handle_rmw_rep_seen_lower_acc(loc_entry, rep, rep_info, is_accept, t_id);
      break;
    case SEEN_HIGHER_ACC:
    case SEEN_HIGHER_PROP:
      handle_rmw_rep_higher_acc_prop(rep, rep_info, is_accept, t_id);
      break;
    case LOG_TOO_HIGH:
      handle_rmw_rep_log_too_high(rep_info);
      break;
    default: my_assert(false, "");
  }
}

// Handle a proposal/accept reply
static inline void handle_prop_or_acc_rep(cp_rmw_rep_mes_t *rep_mes,
                                          cp_rmw_rep_t *rep,
                                          loc_entry_t *loc_entry,
                                          bool is_accept,
                                          const uint16_t t_id)
{
  rmw_rep_info_t *rep_info = &loc_entry->rmw_reps;
  checks_when_handling_prop_acc_rep(loc_entry, rep, is_accept, t_id);
  bookkeeping_for_rep_info(rep_info, rep);
  handle_rmw_rep_based_on_opcode(rep_mes, loc_entry, rep, rep_info, is_accept, t_id);
  check_handle_rmw_rep_end(loc_entry, is_accept);
}


// Handle one accept or propose reply
static inline void find_local_and_handle_rmw_rep(loc_entry_t *loc_entry_array,
                                                 cp_rmw_rep_t *rep,
                                                 cp_rmw_rep_mes_t *rep_mes,
                                                 uint16_t byte_ptr,
                                                 bool is_accept,
                                                 uint16_t r_rep_i,
                                                 uint16_t t_id)
{
  check_find_local_and_handle_rmw_rep(loc_entry_array, rep, rep_mes,
                                      byte_ptr, is_accept, r_rep_i, t_id);

  int entry_i = search_prop_entries_with_l_id(loc_entry_array, (uint8_t) (is_accept ? ACCEPTED : PROPOSED),
                                              rep->l_id);
  if (entry_i == -1) return;
  loc_entry_t *loc_entry = &loc_entry_array[entry_i];
  handle_prop_or_acc_rep(rep_mes, rep, loc_entry, is_accept, t_id);
}

// Handle read replies that refer to RMWs (either replies to accepts or proposes)
static inline void handle_rmw_rep_replies(loc_entry_t *loc_entry_array, cp_rmw_rep_mes_t *r_rep_mes,
                                          bool is_accept, uint16_t t_id)
{
  cp_rmw_rep_mes_t *rep_mes = (cp_rmw_rep_mes_t *) r_rep_mes;
  check_state_with_allowed_flags(4, r_rep_mes->opcode, ACCEPT_REPLY,
                                 PROP_REPLY, ACCEPT_REPLY_NO_CREDITS);
  uint8_t rep_num = rep_mes->coalesce_num;
  //my_printf(yellow, "Received opcode %u, prop_rep num %u \n", r_rep_mes->opcode, rep_num);
  uint16_t byte_ptr = RMW_REP_MES_HEADER; // same for both accepts and replies
  for (uint16_t r_rep_i = 0; r_rep_i < rep_num; r_rep_i++) {
    struct rmw_rep_last_committed *rep = (struct rmw_rep_last_committed *) (((void *) rep_mes) + byte_ptr);
    find_local_and_handle_rmw_rep(loc_entry_array, rep, rep_mes, byte_ptr, is_accept, r_rep_i, t_id);
    byte_ptr += get_size_from_opcode(rep->opcode);
  }
  r_rep_mes->opcode = INVALID_OPCODE;
}





/*--------------------------------------------------------------------------
 * -----------------------------RMW-FSM-------------------------------------
 * --------------------------------------------------------------------------*/


//------------------------------HELP STUCK RMW------------------------------------------
// When time-out-ing on a stuck Accepted value, and try to help it, you need to first propose your own
static inline void set_up_a_proposed_but_not_locally_acked_entry(sess_stall_t *stall_info, mica_op_t  *kv_ptr,
                                                                 loc_entry_t *loc_entry,
                                                                 bool help_myself, uint16_t t_id)
{
  checks_and_prints_proposed_but_not_locally_acked(stall_info, kv_ptr, loc_entry, t_id);
  loc_entry_t *help_loc_entry = loc_entry->help_loc_entry;
  loc_entry->state = PROPOSED;
  help_loc_entry->state = ACCEPTED;
  if (help_myself) {
    loc_entry->helping_flag = PROPOSE_LOCALLY_ACCEPTED;
  }
  else {
    loc_entry->helping_flag = PROPOSE_NOT_LOCALLY_ACKED;
    help_loc_entry->log_no = loc_entry->log_no;
    help_loc_entry->key = loc_entry->key;
  }
  loc_entry->rmw_reps.tot_replies = 1;
  loc_entry->rmw_reps.already_accepted = 1;
  logging_proposed_but_not_locally_acked(kv_ptr, loc_entry, help_loc_entry, t_id);
}


// When inspecting an RMW that failed to grab a kv_ptr in the past
static inline bool attempt_to_grab_kv_ptr_after_waiting(sess_stall_t *stall_info,
                                                        mica_op_t *kv_ptr,
                                                        loc_entry_t *loc_entry,
                                                        uint16_t sess_i, uint16_t t_id)
{
  checks_init_attempt_to_grab_kv_ptr(loc_entry, t_id);
  bool kv_ptr_was_grabbed = false;
  bool rmw_fails = false;
  uint32_t version = PAXOS_TS;

  lock_kv_ptr(kv_ptr, t_id);
  if (if_already_committed_bcast_commits(loc_entry, t_id)) {
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);
    return true;
  }
  if (kv_ptr->state == INVALID_RMW) {
    if (!rmw_fails_with_loc_entry(loc_entry, kv_ptr, &rmw_fails, t_id)) {
      loc_entry->log_no = kv_ptr->last_committed_log_no + 1;
      activate_kv_pair(PROPOSED, PAXOS_TS, kv_ptr, loc_entry->opcode,
                       (uint8_t) machine_id, NULL, loc_entry->rmw_id.id,
                       loc_entry->log_no, t_id,
                       ENABLE_ASSERTIONS ? "attempt_to_grab_kv_ptr_after_waiting" : NULL);

      kv_ptr_was_grabbed = true;
      print_when_grabbing_kv_ptr(loc_entry, t_id);
    }
  }
  else if (kv_ptr_state_has_changed(kv_ptr, loc_entry->help_rmw)) {
    print_when_state_changed_not_grabbing_kv_ptr(kv_ptr, loc_entry, t_id);
    loc_entry->help_rmw->state = kv_ptr->state;
    assign_second_rmw_id_to_first(&loc_entry->help_rmw->rmw_id, &kv_ptr->rmw_id);
    loc_entry->help_rmw->ts = kv_ptr->prop_ts;
    loc_entry->help_rmw->log_no = kv_ptr->log_no;
    loc_entry->back_off_cntr = 0;
  }
  check_log_nos_of_kv_ptr(kv_ptr, "attempt_to_grab_kv_ptr_after_waiting", t_id);
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);
  if (kv_ptr_was_grabbed) {
    fill_loc_rmw_entry_on_grabbing_kv_ptr(loc_entry, PAXOS_TS,
                                          PROPOSED, sess_i, t_id);
  }
  else if (rmw_fails) {
    check_and_print_when_rmw_fails(kv_ptr, loc_entry, t_id);
    loc_entry->state = INVALID_RMW;
    free_session_from_rmw(loc_entry, stall_info, false, t_id);
    return true;
  }
  return kv_ptr_was_grabbed;
}

// Insert a helping accept in the write fifo after waiting on it
static inline void attempt_to_help_a_locally_accepted_value(sess_stall_t *stall_info,
                                                            loc_entry_t *loc_entry,
                                                            mica_op_t *kv_ptr, uint16_t t_id)
{
  bool help = false;
  loc_entry_t *help_loc_entry = loc_entry->help_loc_entry;
  // The stat of the kv_ptr must not be downgraded from ACCEPTED
  lock_kv_ptr(loc_entry->kv_ptr, t_id);
  // check again with the lock in hand
  if (kv_ptr_state_has_not_changed(kv_ptr, loc_entry->help_rmw)) {
    loc_entry->log_no = kv_ptr->accepted_log_no;
    help_loc_entry->new_ts = kv_ptr->accepted_ts;
    help_loc_entry->rmw_id = kv_ptr->rmw_id;
    memcpy(help_loc_entry->value_to_write, kv_ptr->last_accepted_value,
           (size_t) RMW_VALUE_SIZE);
    help_loc_entry->base_ts = kv_ptr->base_acc_ts;

    // we must make it appear as if the kv_ptr has seen our propose
    // and has replied with a lower-base_ts-accept
    loc_entry->new_ts.version = kv_ptr->prop_ts.version + 1;
    loc_entry->new_ts.m_id = (uint8_t) machine_id;
    kv_ptr->prop_ts = loc_entry->new_ts;
    help = true;
    checks_attempt_to_help_locally_accepted(kv_ptr, loc_entry, t_id);
  }
  check_log_nos_of_kv_ptr(kv_ptr, "attempt_to_help_a_locally_accepted_value", t_id);
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);

  loc_entry->back_off_cntr = 0;
  if (help) {
    // Helping means we are proposing, but we are not locally acked:
    // We store a reply from the local machine that says already ACCEPTED
    bool helping_myself = help_loc_entry->rmw_id.id == loc_entry->rmw_id.id;
    set_up_a_proposed_but_not_locally_acked_entry(stall_info, kv_ptr, loc_entry, helping_myself, t_id);
  }
}

// After backing off waiting on a PROPOSED kv_ptr try to steal it
static inline void attempt_to_steal_a_proposed_kv_ptr(loc_entry_t *loc_entry,
                                                      mica_op_t *kv_ptr,
                                                      uint16_t sess_i, uint16_t t_id)
{
  bool kv_ptr_was_grabbed = false;
  lock_kv_ptr(loc_entry->kv_ptr, t_id);
  if (if_already_committed_bcast_commits(loc_entry, t_id)) {
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);
    return ;
  }
  uint32_t new_version = 0;

  if (kv_ptr->state == INVALID_RMW || kv_ptr_state_has_not_changed(kv_ptr, loc_entry->help_rmw)) {
    check_the_proposed_log_no(kv_ptr, loc_entry, t_id);
    loc_entry->log_no = kv_ptr->last_committed_log_no + 1;
    new_version = kv_ptr->prop_ts.version + 1;
    activate_kv_pair(PROPOSED, new_version, kv_ptr, loc_entry->opcode,
                     (uint8_t) machine_id, NULL, loc_entry->rmw_id.id,
                     loc_entry->log_no, t_id,
                     ENABLE_ASSERTIONS ? "attempt_to_steal_a_proposed_kv_ptr" : NULL);
    loc_entry->base_ts = kv_ptr->ts;
    kv_ptr_was_grabbed = true;
  }
  else if (kv_ptr_state_has_changed(kv_ptr, loc_entry->help_rmw)) {
    print_when_state_changed_steal_proposed(kv_ptr, loc_entry, t_id);
    loc_entry->help_rmw->log_no = kv_ptr->log_no;
    loc_entry->help_rmw->state = kv_ptr->state;
    loc_entry->help_rmw->ts = kv_ptr->prop_ts;
    assign_second_rmw_id_to_first(&loc_entry->help_rmw->rmw_id, &kv_ptr->rmw_id);
  }
  else if (ENABLE_ASSERTIONS) assert(false);
  check_log_nos_of_kv_ptr(kv_ptr, "attempt_to_steal_a_proposed_kv_ptr", t_id);
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);
  loc_entry->back_off_cntr = 0;
  if (kv_ptr_was_grabbed) {
    print_after_stealing_proposed(kv_ptr, loc_entry, t_id);
    fill_loc_rmw_entry_on_grabbing_kv_ptr(loc_entry, new_version,
                                          PROPOSED, sess_i, t_id);
  }
}


//------------------------------ALREADY-COMMITTED------------------------------------------

//When inspecting an accept/propose and have received already-committed Response
static inline void handle_already_committed_rmw(sess_stall_t *stall_info,
                                                loc_entry_t *loc_entry,
                                                uint16_t t_id)
{
  // Broadcast commits iff you got back you own RMW
  if (!loc_entry->rmw_reps.no_need_to_bcast &&
      (loc_entry->rmw_reps.rmw_id_commited < REMOTE_QUORUM)) {
    // Here we know the correct value/log to broadcast: it's the locally accepted ones
    loc_entry->log_no = loc_entry->accepted_log_no;
    loc_entry->state = MUST_BCAST_COMMITS;
    if (MACHINE_NUM <= 3 && ENABLE_ASSERTIONS) assert(false);
  }
  else {
    //free the session here as well
    loc_entry->state = INVALID_RMW;
    free_session_from_rmw(loc_entry, stall_info, true, t_id);
  }
}




//------------------------------ACKS------------------------------------------
// If a quorum of proposal acks have been gathered, try to broadcast accepts
static inline void act_on_quorum_of_prop_acks(context_t *ctx,
                                              loc_entry_t *loc_entry,
                                              uint16_t t_id)
{
  // first we need to accept locally,
  attempt_local_accept(loc_entry, t_id);
  checks_acting_on_quorum_of_prop_ack(loc_entry, t_id);

  if (loc_entry->state == ACCEPTED) {
    zero_out_the_rmw_reply_loc_entry_metadata(loc_entry);
    local_rmw_ack(loc_entry);
    check_loc_entry_metadata_is_reset(loc_entry, "act_on_quorum_of_prop_acks", t_id);
    cp_acc_insert(ctx, loc_entry, false);
    loc_entry->killable = false;
  }
}





//------------------------------SEEN-LOWER_ACCEPT------------------------------------------
// When a quorum of prop replies have been received, and one of the replies says it has accepted an RMW with lower TS
static inline void act_on_receiving_already_accepted_rep_to_prop(context_t *ctx,
                                                                 loc_entry_t* loc_entry,
                                                                 uint16_t t_id)
{
  checks_acting_on_already_accepted_rep(loc_entry, t_id);
  attempt_local_accept_to_help(loc_entry, t_id);
  if (loc_entry->state == ACCEPTED) {
    loc_entry->helping_flag = HELPING;
    zero_out_the_rmw_reply_loc_entry_metadata(loc_entry);
    local_rmw_ack(loc_entry);
    cp_acc_insert(ctx, loc_entry->help_loc_entry, true);
  }

}


//------------------------------LOG-TOO_HIGH------------------------------------------

static inline void react_on_log_too_high_for_prop(loc_entry_t *loc_entry,
                                                  uint16_t t_id)
{
  loc_entry->state = RETRY_WITH_BIGGER_TS;
  loc_entry->log_too_high_cntr++;
  if (loc_entry->log_too_high_cntr == LOG_TOO_HIGH_TIME_OUT) {
    if (ENABLE_ASSERTIONS) {
      my_printf(red, "Timed out on log_too-high\n",
                t_id, loc_entry->sess_id);
      print_loc_entry(loc_entry, yellow, t_id);
    }
    mica_op_t *kv_ptr = loc_entry->kv_ptr;
    lock_kv_ptr(kv_ptr, t_id);
    if (kv_ptr->last_committed_log_no + 1 == loc_entry->log_no) {
      loc_entry->state = MUST_BCAST_COMMITS_FROM_HELP;
      loc_entry_t *help_loc_entry = loc_entry->help_loc_entry;
      memcpy(help_loc_entry->value_to_write, kv_ptr->value, (size_t) VALUE_SIZE);
      help_loc_entry->rmw_id = kv_ptr->last_committed_rmw_id;
      help_loc_entry->base_ts = kv_ptr->ts;
    }
    unlock_kv_ptr(loc_entry->kv_ptr, t_id);

    if (unlikely(loc_entry->state == MUST_BCAST_COMMITS_FROM_HELP)) {
      loc_entry->helping_flag = HELP_PREV_COMMITTED_LOG_TOO_HIGH;
      loc_entry->help_loc_entry->log_no = loc_entry->log_no - 1;
      loc_entry->help_loc_entry->key = loc_entry->key;
    }

    loc_entry->log_too_high_cntr = 0;
  }
}


//------------------------------CLEAN-UP------------------------------------------

static inline void clean_up_after_retrying(sess_stall_t *stall_info,
                                           mica_op_t *kv_ptr,
                                           loc_entry_t *loc_entry,
                                           bool kv_ptr_was_grabbed,
                                           bool help_locally_acced,
                                           bool rmw_fails,
                                           uint16_t t_id)
{
  if (kv_ptr_was_grabbed) {
    print_clean_up_after_retrying(kv_ptr, loc_entry, t_id);
    loc_entry->state = PROPOSED;
    if (help_locally_acced)
      set_up_a_proposed_but_not_locally_acked_entry(stall_info, kv_ptr,
                                                    loc_entry, true, t_id);
    else local_rmw_ack(loc_entry);
  }
  else if (rmw_fails) {
    check_clean_up_after_retrying(kv_ptr, loc_entry,
                                  help_locally_acced, t_id);
    loc_entry->state = INVALID_RMW;
    free_session_from_rmw(loc_entry, stall_info,
                          false, t_id);
  }
  else loc_entry->state = NEEDS_KV_PTR;
}



static inline void clean_up_after_inspecting_props(loc_entry_t *loc_entry,
                                                   bool zero_out_log_too_high_cntr,
                                                   uint16_t t_id)
{
  checks_before_resetting_prop(loc_entry);
  if (loc_entry->helping_flag == PROPOSE_NOT_LOCALLY_ACKED ||
      loc_entry->helping_flag == PROPOSE_LOCALLY_ACCEPTED)
    loc_entry->helping_flag = NOT_HELPING;

  if (loc_entry->rmw_reps.ready_to_inspect)
    zero_out_the_rmw_reply_loc_entry_metadata(loc_entry);
  else if (ENABLE_ASSERTIONS) assert(loc_entry->rmw_reps.tot_replies == 1);

  if (zero_out_log_too_high_cntr) loc_entry->log_too_high_cntr = 0;

  set_kilalble_flag(loc_entry);
  check_after_inspecting_prop(loc_entry);
}



static inline void clean_up_after_inspecting_accept(loc_entry_t *loc_entry,
                                                    uint16_t t_id)
{
  checks_before_resetting_accept(loc_entry);
  advance_loc_entry_l_id(loc_entry, t_id);
  zero_out_the_rmw_reply_loc_entry_metadata(loc_entry);
  reset_all_aboard_accept(loc_entry, t_id);
  check_after_inspecting_accept(loc_entry);
}


//------------------------------REGULAR INSPECTIONS------------------------------------------

// local_entry->state == RETRY_WITH_BIGGER_TS
static inline void take_kv_ptr_with_higher_TS(sess_stall_t *stall_info,
                                              loc_entry_t *loc_entry,
                                              bool from_propose,
                                              uint16_t t_id) {
  bool kv_ptr_was_grabbed  = false,
      is_still_proposed, is_still_accepted, kv_ptr_can_be_taken_with_higher_TS;
  bool rmw_fails = false;
  bool help = false;
  mica_op_t *kv_ptr = loc_entry->kv_ptr;
  lock_kv_ptr(kv_ptr, t_id);
  {
    if (if_already_committed_bcast_commits(loc_entry, t_id)) {
      unlock_kv_ptr(loc_entry->kv_ptr, t_id);
      return;
    }
    is_still_proposed = rmw_ids_are_equal(&kv_ptr->rmw_id, &loc_entry->rmw_id) &&
                        kv_ptr->state == PROPOSED;

    is_still_accepted = rmw_ids_are_equal(&kv_ptr->rmw_id, &loc_entry->rmw_id) &&
                        kv_ptr->state == ACCEPTED &&
                        compare_ts(&kv_ptr->accepted_ts, &loc_entry->new_ts) == EQUAL;
    kv_ptr_can_be_taken_with_higher_TS =
        kv_ptr->state == INVALID_RMW || is_still_proposed || is_still_accepted;

    // if either state is invalid or we own it
    if (kv_ptr_can_be_taken_with_higher_TS) {
      if (rmw_fails_with_loc_entry(loc_entry, kv_ptr, &rmw_fails, t_id)) {
        if (ENABLE_ASSERTIONS) assert(kv_ptr->state != ACCEPTED);
        kv_ptr->state = INVALID_RMW;
      }
      else {
        if (kv_ptr->state == INVALID_RMW) {
          kv_ptr->log_no = kv_ptr->last_committed_log_no + 1;
          kv_ptr->opcode = loc_entry->opcode;
          assign_second_rmw_id_to_first(&kv_ptr->rmw_id, &loc_entry->rmw_id);
        } else if (ENABLE_ASSERTIONS) {
          assert(loc_entry->log_no == kv_ptr->last_committed_log_no + 1);
          assert(kv_ptr->log_no == kv_ptr->last_committed_log_no + 1);
          if (kv_ptr->state == ACCEPTED) {
            assert(!from_propose);
            assert(compare_ts(&kv_ptr->accepted_ts, &loc_entry->new_ts) == EQUAL);
          }
        }
        loc_entry->log_no = kv_ptr->last_committed_log_no + 1;
        loc_entry->new_ts.version = MAX(loc_entry->new_ts.version, kv_ptr->prop_ts.version) + 1;
        loc_entry->base_ts = kv_ptr->ts; // Minimize the possibility for RMW_ACK_BASE_TS_STALE
        if (ENABLE_ASSERTIONS) {
          assert(loc_entry->new_ts.version > kv_ptr->prop_ts.version);
        }
        loc_entry->new_ts.m_id = (uint8_t) machine_id;
        kv_ptr->prop_ts = loc_entry->new_ts;
        if (!is_still_accepted) {
          if (ENABLE_ASSERTIONS) assert(kv_ptr->state != ACCEPTED);
          kv_ptr->state = PROPOSED;
        }
          //PROPOSE_LOCALLY_ACCEPTED
        else {
          // Attention: when retrying an RMW that has been locally accepted,
          // you need to start from Proposes, but the kv_ptr can NOT be downgraded to proposed
          help = true;
          //loc_entry->helping_flag = PROPOSE_LOCALLY_ACCEPTED;
          loc_entry->help_loc_entry->new_ts = kv_ptr->accepted_ts;
        }
        kv_ptr_was_grabbed = true;
      }
    } else {
      if (DEBUG_RMW)
        my_printf(yellow, "Wrkr %u, session %u  failed when attempting to get/regain the kv_ptr, "
                          "waiting: waited for %u cycles for "
                          "now waiting on rmw_id %, state %u\n",
                  t_id, loc_entry->sess_id,
                  kv_ptr->rmw_id.id, kv_ptr->state);
    }
    check_log_nos_of_kv_ptr(kv_ptr, "take_kv_ptr_with_higher_TS", t_id);
  }
  unlock_kv_ptr(loc_entry->kv_ptr, t_id);

  if (ENABLE_ASSERTIONS) if (is_still_accepted) assert(help);
  clean_up_after_retrying(stall_info, kv_ptr, loc_entry,
                          kv_ptr_was_grabbed, is_still_accepted,
                          rmw_fails, t_id);


}


// local_entry->state = NEEDS_KV_PTR
static inline void handle_needs_kv_ptr_state(context_t *ctx,
                                             sess_stall_t *stall_info,
                                             loc_entry_t *loc_entry,
                                             uint16_t sess_i,
                                             uint16_t t_id)
{
  mica_op_t *kv_ptr = loc_entry->kv_ptr;

  // If this fails to grab a kv_ptr it will try to update
  // the (rmw_id + state) that is being waited on.
  // If it updates it will zero the back-off counter
  if (!attempt_to_grab_kv_ptr_after_waiting(stall_info, kv_ptr, loc_entry,
                                            sess_i, t_id)) {
    if (ENABLE_ASSERTIONS) assert(stall_info->stalled);
    loc_entry->back_off_cntr++;
    if (loc_entry->back_off_cntr == RMW_BACK_OFF_TIMEOUT) {
      //   my_printf(yellow, "Wrkr %u  sess %u waiting for an rmw on key %u on log %u, back_of cntr %u waiting on rmw_id %u state %u \n",
      //                 t_id, sess_i,loc_entry->key.bkt, loc_entry->help_rmw->log_no, loc_entry->back_off_cntr,
      //                 loc_entry->help_rmw->rmw_id.id,
      //                 loc_entry->help_rmw->state);

      // This is failure-related help/stealing it should not be that we are being held up by the local machine
      // However we may wait on a "local" glob sess id, because it is being helped
      // if have accepted a value help it
      if (loc_entry->help_rmw->state == ACCEPTED)
        attempt_to_help_a_locally_accepted_value(stall_info, loc_entry, kv_ptr, t_id); // zeroes the back-off counter
        // if have received a proposal, send your own proposal
      else  if (loc_entry->help_rmw->state == PROPOSED) {
        attempt_to_steal_a_proposed_kv_ptr(loc_entry, kv_ptr, sess_i, t_id); // zeroes the back-off counter
      }
    }
  }
  if (loc_entry->state == PROPOSED) {
    loc_entry->back_off_cntr = 0;
    cp_prop_insert(ctx, loc_entry);
  }
  check_state_with_allowed_flags(6, (int) loc_entry->state, INVALID_RMW, PROPOSED, NEEDS_KV_PTR,
                                 ACCEPTED, MUST_BCAST_COMMITS);

}

// Inspect each propose that has gathered a quorum of replies
static inline void inspect_proposes(context_t *ctx,
                                    sess_stall_t *stall_info,
                                    loc_entry_t *loc_entry,
                                    uint16_t t_id)
{
  if (not_ready_to_inspect_propose(loc_entry)) return;
  loc_entry->stalled_reason = NO_REASON;
  loc_entry->rmw_reps.inspected = true;
  advance_loc_entry_l_id(loc_entry, t_id);
  bool zero_out_log_too_high_cntr = true;
  struct rmw_rep_info *rep_info = &loc_entry->rmw_reps;
  uint8_t remote_quorum = QUORUM_NUM;

  // RMW_ID COMMITTED
  if (rep_info->rmw_id_commited > 0) {
    debug_fail_help(loc_entry, " rmw id committed", t_id);
    // as an optimization clear the kv_ptr entry if it is still in proposed state
    if (loc_entry->accepted_log_no != loc_entry->log_no)
      free_kv_ptr_if_rmw_failed(loc_entry, PROPOSED, t_id);
    handle_already_committed_rmw(stall_info, loc_entry, t_id);
    check_state_with_allowed_flags(3, (int) loc_entry->state, INVALID_RMW,
                                   MUST_BCAST_COMMITS);
  }
    // LOG_NO TOO SMALL
  else if (rep_info->log_too_small > 0) {
    debug_fail_help(loc_entry, " log too small", t_id);
    //It is impossible for this RMW to still hold the kv_ptr
    loc_entry->state = NEEDS_KV_PTR;
  }
    // SEEN HIGHER-TS PROPOSE OR ACCEPT
  else if (rep_info->seen_higher_prop_acc > 0) {
    debug_fail_help(loc_entry, " seen higher prop", t_id);
    // retry by incrementing the highest base_ts seen
    loc_entry->state = RETRY_WITH_BIGGER_TS;
    loc_entry->new_ts.version = rep_info->seen_higher_prop_version;
  }
    // ACK QUORUM
  else if (rep_info->acks >= remote_quorum &&
           loc_entry->helping_flag != PROPOSE_NOT_LOCALLY_ACKED) {
    debug_fail_help(loc_entry, " quorum", t_id);
    // Quorum of prop acks gathered: send an accept
    act_on_quorum_of_prop_acks(ctx, loc_entry, t_id);
    check_state_with_allowed_flags(4, (int) loc_entry->state,
                                   ACCEPTED, NEEDS_KV_PTR, MUST_BCAST_COMMITS);
  }
    // ALREADY ACCEPTED AN RMW WITH LOWER_TS
  else if (rep_info->already_accepted > 0) {
    debug_fail_help(loc_entry, " already accepted", t_id);
    if (loc_entry->helping_flag == PROPOSE_LOCALLY_ACCEPTED)
      act_on_quorum_of_prop_acks(ctx, loc_entry, t_id);
    else act_on_receiving_already_accepted_rep_to_prop(ctx, loc_entry, t_id);
    check_state_with_allowed_flags(4, (int) loc_entry->state, ACCEPTED,
                                   NEEDS_KV_PTR, MUST_BCAST_COMMITS);
  }
    // LOG TOO HIGH
  else if (rep_info->log_too_high > 0) {
    react_on_log_too_high_for_prop(loc_entry, t_id);
    loc_entry->new_ts.version = loc_entry->new_ts.version;
    zero_out_log_too_high_cntr = false;
  }
  else if (ENABLE_ASSERTIONS) assert(false);

  clean_up_after_inspecting_props(loc_entry, zero_out_log_too_high_cntr, t_id);
}


// Inspect each propose that has gathered a quorum of replies
static inline void inspect_accepts(sess_stall_t *stall_info,
                                   loc_entry_t *loc_entry,
                                   uint16_t t_id)
{
  check_sum_of_reps(loc_entry);
  if (!loc_entry->rmw_reps.ready_to_inspect) return;
  loc_entry->rmw_reps.inspected = true;
  struct rmw_rep_info *rep_info = &loc_entry->rmw_reps;
  uint8_t remote_quorum = (uint8_t) (loc_entry->all_aboard ?
                                     MACHINE_NUM : QUORUM_NUM);
  //uint32_t new_version = 0;
  if (loc_entry->helping_flag != NOT_HELPING &&
      rep_info->rmw_id_commited  + rep_info->log_too_small +
      rep_info->already_accepted + rep_info->seen_higher_prop_acc +
      rep_info->log_too_high > 0)
  {
    if (ENABLE_ASSERTIONS) assert(loc_entry->helping_flag == HELPING);
    reinstate_loc_entry_after_helping(loc_entry, t_id);

  }
    // RMW_ID COMMITTED
  else if (rep_info->rmw_id_commited > 0) {
    handle_already_committed_rmw(stall_info, loc_entry, t_id);
    check_state_with_allowed_flags(3, (int) loc_entry->state, INVALID_RMW,
                                   MUST_BCAST_COMMITS);
    if (ENABLE_ASSERTIONS) assert(loc_entry->helping_flag == NOT_HELPING);
  }
    // LOG_NO TOO SMALL
  else if (rep_info->log_too_small > 0) {
    //It is impossible for this RMW to still hold the kv_ptr
    loc_entry->state = NEEDS_KV_PTR;
    if (ENABLE_ASSERTIONS) assert(loc_entry->helping_flag == NOT_HELPING);
  }
    // ACK QUORUM
  else if (rep_info->acks >= remote_quorum) {
    bookkeeping_after_gathering_accept_acks(loc_entry, t_id);
    loc_entry->state = (uint8_t) (loc_entry->helping_flag == HELPING ?
                                  MUST_BCAST_COMMITS_FROM_HELP : MUST_BCAST_COMMITS);
  }
    // SEEN HIGHER-TS PROPOSE
  else if (rep_info->seen_higher_prop_acc > 0) {
    // retry by incrementing the highest base_ts seen
    loc_entry->state = RETRY_WITH_BIGGER_TS;
    loc_entry->new_ts.version = rep_info->seen_higher_prop_version;
    if (ENABLE_ASSERTIONS) assert(loc_entry->helping_flag == NOT_HELPING);
  }
    // LOG TOO HIGH
  else if (rep_info->log_too_high > 0) {
    loc_entry->state = RETRY_WITH_BIGGER_TS;
  }
    // if a quorum of messages have been received but
    // we are waiting for more, then we are doing all aboard
  else if (ENABLE_ALL_ABOARD) {
    if (ENABLE_ASSERTIONS) assert(loc_entry->all_aboard);
    loc_entry->all_aboard_time_out++;
    if (ENABLE_ASSERTIONS) assert(loc_entry->new_ts.version == ALL_ABOARD_TS);
    if (loc_entry->all_aboard_time_out > ALL_ABOARD_TIMEOUT_CNT) {
      // printf("Wrkr %u, Timing out on key %u \n", t_id, loc_entry->key.bkt);
      loc_entry->state = RETRY_WITH_BIGGER_TS;
      loc_entry->all_aboard_time_out = 0;
      loc_entry->new_ts.version = PAXOS_TS;
    }
    else return; // avoid zeroing out the responses
  }
  else if (ENABLE_ASSERTIONS) assert(false);


  clean_up_after_inspecting_accept(loc_entry, t_id);

}



static inline bool inspect_commits(context_t *ctx,
                                   loc_entry_t* loc_entry,
                                   uint32_t commit_capacity)
{

  loc_entry_t *entry_to_commit =
      loc_entry->state == MUST_BCAST_COMMITS ? loc_entry : loc_entry->help_loc_entry;
  if (commit_capacity < COM_ROB_SIZE) {
    print_commit_latest_committed(loc_entry, ctx->t_id);
    cp_com_insert(ctx, entry_to_commit, loc_entry->state);
    loc_entry->state = COMMITTED;
    return true;
  }
  return false;
}


/*--------------------------------------------------------------------------
 * --------------------INIT RMW-------------------------------------
 * --------------------------------------------------------------------------*/

// Insert an RMW in the local RMW structs
static inline void insert_rmw(context_t *ctx,
                              loc_entry_t *loc_entry_array,
                              sess_stall_t *stall_info,
                              trace_op_t *op,
                              uint16_t t_id)
{
  uint16_t session_id = op->session_id;
  loc_entry_t *loc_entry = &loc_entry_array[session_id];
  if (loc_entry->state == CAS_FAILED) {
    //printf("Wrkr%u, sess %u, entry %u rmw_failing \n", t_id, session_id, resp->rmw_entry);
    signal_completion_to_client(session_id, op->index_to_req_array, t_id);
    stall_info->stalled[session_id] = false;
    stall_info->all_stalled = false;
    loc_entry->state = INVALID_RMW;
    return;
  }
  if (ENABLE_ASSERTIONS) {
    assert(session_id < SESSIONS_PER_THREAD);
  }

  // my_printf(green, "Session %u starts an rmw \n", loc_entry->glob_sess_id);
  uint8_t state = (uint8_t) (ENABLE_ALL_ABOARD && op->attempt_all_aboard ? ACCEPTED: PROPOSED);
  // if the kv_ptr was occupied, put in the next op to try next round
  if (loc_entry->state == state) { // the RMW has gotten an entry and is to be sent
    fill_loc_rmw_entry_on_grabbing_kv_ptr(loc_entry, op->ts.version,
                                          state, session_id, t_id);
    if (state == ACCEPTED) {
      if (ENABLE_ASSERTIONS) {
        assert(op->ts.version == ALL_ABOARD_TS);
      }
      loc_entry->accepted_log_no = loc_entry->log_no;
      cp_acc_insert(ctx, loc_entry, false);
      loc_entry->killable = false;
      loc_entry->all_aboard = true;
      loc_entry->all_aboard_time_out = 0;
    }
    else {
      if (ENABLE_ASSERTIONS) assert(op->ts.version == PAXOS_TS);
      cp_prop_insert(ctx, loc_entry);
    }
  }
  else if (loc_entry->state == NEEDS_KV_PTR) {
    if (ENABLE_ALL_ABOARD) loc_entry->all_aboard = false;
  }
  else my_assert(false, "Wrong loc_entry in RMW");
}





#endif //CP_CORE_UTIL_H
