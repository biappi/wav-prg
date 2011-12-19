#include "wav2prg_api.h"
#include "wav2prg_block_list.h"
#include "get_pulse.h"

#include <stdlib.h>
#include <limits.h>
#include <string.h>

enum wav2prg_bool get_pulse_tolerant(uint32_t raw_pulse, struct wav2prg_plugin_conf* conf, uint8_t* pulse)
{
  for (*pulse = 0; *pulse < conf->num_pulse_lengths - 1; *pulse++) {
    if (raw_pulse < conf->thresholds[*pulse])
      return wav2prg_true;
  }
  return wav2prg_true;
}

struct tolerance {
  uint16_t min;
  uint16_t max;
};

struct tolerances {
  struct tolerance measured;
  struct tolerance range;
  uint32_t statistics;
  float average;
};

static struct {
  struct
  {
    uint8_t num_pulse_lengths;
    uint16_t *thresholds;
  };
  struct tolerances *tolerances;
} *store = NULL;
static uint32_t store_size = 0;

static struct tolerances** get_existing_tolerances_and_possibly_modify_them(uint8_t num_pulse_lengths, const uint16_t *thresholds)
{
  uint32_t i;

  for(i = 0; i < store_size; i++){
    if(num_pulse_lengths == store[i].num_pulse_lengths){
      uint32_t j;
      for (j = 0; j < num_pulse_lengths - 1; j++)
        if (thresholds[j] == store[i].thresholds[j])
          break;
      if (j < num_pulse_lengths - 1)
        return &store[i].tolerances;
    }
  }
  return NULL;
}

const struct tolerances* get_existing_tolerances(uint8_t num_pulse_lengths, const uint16_t *thresholds)
{
  struct tolerances** t = get_existing_tolerances_and_possibly_modify_them(num_pulse_lengths, thresholds);
  return t ? *t : NULL;
}

void copy_tolerances(uint8_t num_pulse_lengths, struct tolerances *dest, const struct tolerances *src)
{
  memcpy(dest, src, sizeof(struct tolerances) * num_pulse_lengths);
}

struct tolerances* new_copy_tolerances(uint8_t num_pulse_lengths, const struct tolerances *src)
{
  struct tolerances* dest = malloc(sizeof(*src) * num_pulse_lengths);
  copy_tolerances(num_pulse_lengths, dest, src);
  return dest;
}

struct tolerances* get_tolerances(uint8_t num_pulse_lengths, const uint16_t *thresholds){
  const struct tolerances *existing_tolerances = get_existing_tolerances(num_pulse_lengths, thresholds);
  struct tolerances *tolerances = malloc(sizeof(*tolerances) * num_pulse_lengths);

  if (existing_tolerances != NULL)
    copy_tolerances(num_pulse_lengths, tolerances, existing_tolerances);
  else {
    uint8_t i;

    for(i = 0; i < num_pulse_lengths; i++){
      tolerances[i].measured.min = USHRT_MAX;
      tolerances[i].measured.max = 0;
      tolerances[i].statistics = 0;
    }
    tolerances[0].range.min = (uint16_t)(thresholds[0] * 0.51);
    tolerances[0].range.max = (uint16_t)(thresholds[0] * 0.96);
    for(i = 1; i < num_pulse_lengths - 1; i++){
      tolerances[i].range.min =
        (uint16_t)(thresholds[i - 1] *  .92 + thresholds[i] * .08);
      tolerances[i].range.max =
        (uint16_t)(thresholds[i - 1] *  .08 + thresholds[i] * .92);
    }
    tolerances[num_pulse_lengths - 1].range.min = (uint16_t)(thresholds[num_pulse_lengths - 2] * 1.04);
    tolerances[num_pulse_lengths - 1].range.max = (uint16_t)(thresholds[num_pulse_lengths - 2] * 1.56);
  }

 return tolerances;
}

void add_or_replace_tolerances(uint8_t num_pulse_lengths, const uint16_t *thresholds, struct tolerances *tolerances)
{
  struct tolerances **existing_tolerances = get_existing_tolerances_and_possibly_modify_them(num_pulse_lengths, thresholds);

  if(existing_tolerances != NULL){
    free(*existing_tolerances);
    *existing_tolerances = tolerances;
    return;
  }

  if (store_size == 0)
    store = malloc(sizeof(*store));
  else
    store = realloc(store, sizeof(*store) * (store_size + 1));
  store[store_size].tolerances = tolerances;
  store[store_size].num_pulse_lengths = num_pulse_lengths;
  store[store_size].thresholds = malloc(sizeof(uint16_t) * (num_pulse_lengths - 1));
  memcpy(store[store_size++].thresholds, thresholds, sizeof(uint16_t) * (num_pulse_lengths - 1));
}

#define MIN_NUM_PULSES_FOR_RELIABLE_STATISTICS 40
#define MAX_DISTANCE 96

static enum wav2prg_bool is_this_pulse_right_intolerant(uint32_t raw_pulse, const struct tolerance *tolerance)
{
  return raw_pulse >= tolerance->min && raw_pulse <= tolerance->max;
}

static enum {
  limited_increment,
  limited_distance_from_average
} mode = limited_increment;

static uint32_t distance = 32;

enum wav2prg_bool set_distance_from_current_edge(const char* v, void *unused){
  distance = atoi(v);
  return wav2prg_true;
}

enum wav2prg_bool set_distance_from_current_average(const char* v, void *unused){
  mode = limited_distance_from_average;
  distance = v ? atoi(v) : 96;
  return wav2prg_true;
}

static uint32_t min_allowed_value(struct tolerances *tolerance){
  switch(mode){
  case limited_increment:
    return tolerance->measured.min - distance;
  case limited_distance_from_average:
    return (uint32_t)(tolerance->average - distance);
  }
}

static uint32_t max_allowed_value(struct tolerances *tolerance){
  switch(mode){
  case limited_increment:
    return tolerance->measured.max + distance;
  case limited_distance_from_average:
    return (uint32_t)(tolerance->average + distance);
  }
}

static enum pulse_right {
  within_range,
  within_measured,
  not_this_pulse
} is_this_pulse_right(uint32_t raw_pulse, struct tolerances *tolerance, int16_t* difference)
{
  if (tolerance->statistics < MIN_NUM_PULSES_FOR_RELIABLE_STATISTICS)
    return is_this_pulse_right_intolerant(raw_pulse, &tolerance->range)
      ? within_range
      : not_this_pulse;
  if(raw_pulse >= tolerance->measured.min
    && raw_pulse <= tolerance->measured.max){
    *difference = 0;
    return within_measured;
  }
  if(raw_pulse < tolerance->measured.min
    && raw_pulse >= min_allowed_value(tolerance)){
    *difference = raw_pulse - tolerance->measured.min;
    return within_measured;
  }
  if(raw_pulse > tolerance->measured.max
    && raw_pulse <= max_allowed_value(tolerance)){
    *difference = raw_pulse - tolerance->measured.max;
    return within_measured;
  }
  return not_this_pulse;
}

static void update_statistics(uint32_t raw_pulse, struct tolerances *tolerance)
{
  if (tolerance->measured.min > raw_pulse)
    tolerance->measured.min = raw_pulse;
  if (tolerance->measured.max < raw_pulse)
    tolerance->measured.max = raw_pulse;
  tolerance->average = ((tolerance->average * tolerance->statistics) + raw_pulse) / (tolerance->statistics + 1);
  tolerance->statistics++;
}

enum wav2prg_bool get_pulse_adaptively_tolerant(uint32_t raw_pulse, uint8_t num_pulse_lengths, struct tolerances *tolerances, uint8_t* pulse)
{
  int16_t lower_difference;
  enum pulse_right is_lower_pulse_right = is_this_pulse_right(raw_pulse, tolerances, &lower_difference);
  enum wav2prg_bool res = wav2prg_false;

  for(*pulse = 0;;(*pulse)++){
    enum pulse_right is_higher_pulse_right;
    int16_t higher_difference;

    if (is_lower_pulse_right == within_range
      || (is_lower_pulse_right == within_measured && lower_difference < 0)){
      res = wav2prg_true;
      break;
    }
    if (*pulse == num_pulse_lengths - 1)
      break;
    is_higher_pulse_right = is_this_pulse_right(raw_pulse, tolerances + *pulse + 1, &higher_difference);
    if (is_lower_pulse_right == within_measured
      && (is_higher_pulse_right == not_this_pulse
      || (is_higher_pulse_right == within_measured
        && lower_difference < -higher_difference))){
      res = wav2prg_true;
      break;
    }
    lower_difference = higher_difference;
    is_lower_pulse_right = is_higher_pulse_right;
  }
  if (!res && is_lower_pulse_right == within_measured)
    res = wav2prg_true;

  if(res)
    update_statistics(raw_pulse, tolerances + *pulse);

  return res;
}

enum wav2prg_bool get_pulse_intolerant(uint32_t raw_pulse, struct tolerances *tolerances, uint8_t num_pulse_lengths, uint8_t* pulse)
{
  for(*pulse = 0; *pulse < num_pulse_lengths; (*pulse)++){
    if (is_this_pulse_right_intolerant(raw_pulse, &tolerances[*pulse].range))
    {
      update_statistics(raw_pulse, tolerances + *pulse);
      return wav2prg_true;
    }
  }
  return wav2prg_false;
}

enum wav2prg_bool get_pulse_in_measured_ranges(uint32_t raw_pulse, const struct tolerances *tolerances, uint8_t num_pulse_lengths, uint8_t* pulse)
{
  for(*pulse = 0; *pulse < num_pulse_lengths; (*pulse)++){
    if (is_this_pulse_right_intolerant(raw_pulse, &tolerances[*pulse].measured))
      return wav2prg_true;
  }
  return wav2prg_false;
}

uint16_t get_average(const struct tolerances *tolerance, uint8_t pulse)
{
  return (uint16_t)((tolerance+pulse)->average);
}

