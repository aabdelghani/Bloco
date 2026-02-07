#pragma once

// UART command handler task - reads JSON commands from UART and dispatches them.
// Commands are newline-delimited JSON on stdin/stdout.
void command_task(void *arg);
