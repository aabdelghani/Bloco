#pragma once

#include "block_types.h"
#include "espnow_protocol.h"

// Execute a received block program
void executor_run(const block_data_t *blocks, uint8_t count);
