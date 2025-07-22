#ifndef _MCP4822_H
#define _MCP4822_H

#ifdef __cplusplus
extern "C" {
#endif
void     mcp4822_init();
uint32_t mcp4822_get_free_buffer_space();
bool     mcp4822_push_sample(uint16_t sample);

#ifdef __cplusplus
}
#endif

#endif