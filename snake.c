#include "wav2prg_api.h"

static enum wav2prg_bool snake_get_block_info(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, struct wav2prg_block_info* info)
{
  uint8_t fileid;

  if (functions->get_byte_func(context, functions, conf, &fileid) == wav2prg_false)
    return wav2prg_false;
  functions->number_to_name_func(fileid, info->name);
  if (functions->get_word_bigendian_func(context, functions, conf, &info->end) == wav2prg_false)
    return wav2prg_false;
  if (functions->get_word_bigendian_func(context, functions, conf, &info->start) == wav2prg_false)
    return wav2prg_false;
  return wav2prg_true;
}

static uint16_t snake_thresholds[]={0x1be};
static uint8_t snake_pilot_sequence[]={0xc8, 0xc3, 0xd4, 0xc9, 0xd2, 0xc4, 0xcc, 0xc5};

static const struct wav2prg_plugin_conf snake =
{
  msbf,
  wav2prg_add_checksum,
  wav2prg_compute_and_check_checksum,
  2,
  snake_thresholds,
  NULL,
  wav2prg_pilot_tone_made_of_0_bits_followed_by_1,
  64 /*ignored*/,
  sizeof(snake_pilot_sequence),
  snake_pilot_sequence,
  0,
  first_to_last,
  NULL
};

static const struct wav2prg_plugin_conf* snake_get_state(void)
{
  return &snake;
}

static uint16_t snake_unencrypted(const struct wav2prg_block* block, uint16_t *offset, uint8_t *snakeblock)
{
  uint16_t i;

  if (block->info.end < 0x900)
    return 0;

  for(i = 0; i + 19 < 0x900 - 0x801; i++){
    if(block->data[i    ] == 0xa9
    && block->data[i + 2] == 0x85
    && block->data[i + 3] == 0x02
    && block->data[i + 4] == 0xa9
    && block->data[i + 6] == 0x85
    && block->data[i + 7] == 0x03
    && block->data[i + 8] == 0xa9
    && block->data[i + 10] == 0x85
    && block->data[i + 11] == 0x04
    && block->data[i + 12] == 0xa9
    && block->data[i + 14] == 0x85
    && block->data[i + 15] == 0x05
    && block->data[i + 16] == 0xa0
    && block->data[i + 17] == 0x00
    && block->data[i + 18] == 0xa2
      ){
      uint16_t first = block->data[i + 1] + (block->data[i + 5] << 8);
      if (first > 0x801){
        uint16_t j;
        first -= 0x801;
        for(j = 0; j < block->data[i + 19] * 256 && first + j < block->info.end - block->info.start; j++)
          snakeblock[j] = block->data[first + j];
        *offset = block->data[i + 9] + (block->data[i + 13] << 8);
        return j;
      }
    }
  }
  return 0;
}

static uint16_t snake_encrypted(const struct wav2prg_block* block, uint16_t *offset, uint8_t *snakeblock)
{
  if(block->info.end <= 0x863)
    return 0;
  if(block->data[0x084f - 0x801] == 0xBD
  && block->data[0x0850 - 0x801] == 0x59
  && block->data[0x0851 - 0x801] == 0x08
  && block->data[0x0852 - 0x801] == 0x49
  && block->data[0x0853 - 0x801] == 0x5A
  && block->data[0x0854 - 0x801] == 0x9D 
  && block->data[0x0855 - 0x801] == 0x59
  && block->data[0x0856 - 0x801] == 0x08
  && block->data[0x0857 - 0x801] == 0xCA){
    uint16_t j;
    uint16_t first = 
       ((block->data[0x085e - 0x801] ^ 0x5a) * 256)
     - 0x801
     + (block->data[0x085d - 0x801] ^ 0x5a);
    *offset = (block->data[0x0862 - 0x801] ^ 0x5a) + ((block->data[0x0863 - 0x801] ^ 0x5a) << 8);
    for(j = 0; j < 256 && first + j < block->info.end - block->info.start; j++)
      snakeblock[j] = block->data[first + j] ^ 0x21;
    for(; j < 512 && first + j < block->info.end - block->info.start; j++)
      snakeblock[j] = block->data[first + j] ^ 0x41;
    for(; j < 768 && first + j < block->info.end - block->info.start; j++)
      snakeblock[j] = block->data[first + j] ^ 0xa3;
    for(; j < 1024 && first + j < block->info.end - block->info.start; j++)
      snakeblock[j] = block->data[first + j] ^ 0x95;
    return j;
  }
  return 0;
}

static enum wav2prg_bool recognize_snake(struct wav2prg_plugin_conf* conf, const struct wav2prg_block* block, struct wav2prg_block_info *info, enum wav2prg_bool *no_gaps_allowed, uint16_t *where_to_search_in_block, wav2prg_change_sync_sequence_length change_sync_sequence_length_func){
  uint16_t offset, i, blocklen;
  uint8_t snakeblock[1024];

  if (block->info.start != 0x801 || block->info.end < 0x0832)
    return wav2prg_false;

  /* SWIV support */
  if(block->data[0x082d - 0x801] == 0xBD
  && block->data[0x082e - 0x801] == 0x3f
  && block->data[0x082f - 0x801] == 0x08
  && block->data[0x0830 - 0x801] == 0x48
  && block->data[0x0831 - 0x801] == 0x18
  && block->data[0x0832 - 0x801] == 0x69
    ){
    conf->sync_sequence[0]=0x85;
    conf->sync_sequence[1]=0x8C;
    conf->sync_sequence[2]=0x8C;
    conf->sync_sequence[3]=0x85;
    conf->sync_sequence[4]=0x88;
    conf->sync_sequence[5]=0x83;
    conf->sync_sequence[6]=0x89;
    conf->sync_sequence[7]=0x8D;
    return wav2prg_true;
  }

  if(
    (blocklen=snake_unencrypted(block, &offset, snakeblock)) == 0
  &&(blocklen=snake_encrypted(block, &offset, snakeblock)) == 0
  )
    return wav2prg_false;

  for (i = 0; i + 2 < blocklen; i++){
    if (snakeblock[i    ] == 0xa9
     && snakeblock[i + 1] == 0x07
     && snakeblock[i + 2] == 0x8d
    ){
      uint16_t where_name_pos_is = snakeblock[i + 3] + (snakeblock[i + 4] << 8);
      if (where_name_pos_is - 1 - offset > 0
      &&  where_name_pos_is + 3 - offset < blocklen
      && snakeblock[where_name_pos_is - 1 - offset] == 0xa0
      && snakeblock[where_name_pos_is + 1 - offset] == 0xd9
         ){
        uint16_t nameloc =
        snakeblock[where_name_pos_is - offset + 2]
     + (snakeblock[where_name_pos_is - offset + 3] << 8);
        if (nameloc - offset > 0 && nameloc - offset + 7 < blocklen){
          int j;
          for(j = 0; j <= 7; j++)
            conf->sync_sequence[j] = snakeblock[nameloc - offset + 7 - j];
          return wav2prg_true;
        }
      }
    }
  }
  return wav2prg_false;
}

static const struct wav2prg_observed_loaders snake_observed_loaders[] = {
  {"kdc", recognize_snake},
  {NULL,NULL}
};

static const struct wav2prg_observed_loaders* snake_get_observed_loaders(void){
  return snake_observed_loaders;
}

static uint8_t snake_compute_checksum_step(struct wav2prg_plugin_conf* conf, uint8_t old_checksum, uint8_t byte, uint16_t location_of_byte) {
  uint8_t new_checksum = old_checksum ^ byte;
  if ((location_of_byte & 0xff) == 0xff)
    new_checksum++;
  return new_checksum;
}

static const struct wav2prg_plugin_functions snake_functions = {
    NULL,
    NULL,
    NULL,
    NULL,
    snake_get_block_info,
    NULL,
    snake_get_state,
    snake_compute_checksum_step,
    NULL,
    snake_get_observed_loaders,
    NULL
};

PLUGIN_ENTRY(snake)
{
  register_loader_func(&snake_functions, "Snake");
}

