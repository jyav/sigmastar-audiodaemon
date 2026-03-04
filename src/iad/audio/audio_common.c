// --- SIGMASTAR HEADERS REMOVED ---
/*
#include "imp/imp_audio.h"
#include "imp/imp_log.h"
*/

// --- SIGMASTAR HEADERS ADDED ---
#include "mi_ao.h"
#include "mi_sys.h"

#include "audio_common.h"
#include "config.h"
#include "logging.h"
#include "output.h"
#include "cJSON.h" // Explicitly included for cJSON_IsNumber

#define TAG "AUDIO_COMMON"

/**
 * Helper function to get a specific audio attribute from the configuration.
 * @param type The type of audio (e.g., AUDIO_INPUT or AUDIO_OUTPUT).
 * @param attr The specific attribute name.
 * @return The cJSON item for the requested attribute.
 */
static cJSON* get_audio_device_attribute(AudioType type, const char *attr) {
    return get_audio_attribute(type, attr);
}

// FIXED: Removed the dangerous free_audio_device_attribute function. 
// cJSON child nodes must never be deleted individually while the master config tree is active.

/**
 * Retrieves the device and channel IDs for audio input or uses defaults.
 * @param aiDevID Pointer to store the retrieved Device ID.
 * @param aiChnID Pointer to store the retrieved Channel ID.
 */
void get_audio_input_device_attributes(int *aiDevID, int *aiChnID) {
    cJSON *aiDevIDItem = get_audio_device_attribute(AUDIO_INPUT, "device_id");
    cJSON *aiChnIDItem = get_audio_device_attribute(AUDIO_INPUT, "channel_id");

    // FIXED: Strict type-checking prevents union memory corruption if user supplies strings
    *aiDevID = (aiDevIDItem && cJSON_IsNumber(aiDevIDItem)) ? aiDevIDItem->valueint : DEFAULT_AI_DEV_ID;
    *aiChnID = (aiChnIDItem && cJSON_IsNumber(aiChnIDItem)) ? aiChnIDItem->valueint : DEFAULT_AI_CHN_ID;
}

/**
 * Retrieves the device and channel IDs for audio output or uses defaults.
 * @param aoDevID Pointer to store the retrieved Device ID.
 * @param aoChnID Pointer to store the retrieved Channel ID.
 */
void get_audio_output_device_attributes(int *aoDevID, int *aoChnID) {
    cJSON *aoDevIDItem = get_audio_device_attribute(AUDIO_OUTPUT, "device_id");
    cJSON *aoChnIDItem = get_audio_device_attribute(AUDIO_OUTPUT, "channel_id");

    // FIXED: Strict type-checking prevents union memory corruption if user supplies strings
    *aoDevID = (aoDevIDItem && cJSON_IsNumber(aoDevIDItem)) ? aoDevIDItem->valueint : DEFAULT_AO_DEV_ID;
    *aoChnID = (aoChnIDItem && cJSON_IsNumber(aoChnIDItem)) ? aoChnIDItem->valueint : DEFAULT_AO_CHN_ID;
}

/**
 * Fetches the audio input attributes from the configuration.
 * @return A structure containing the audio input attributes.
 */
AudioInputAttributes get_audio_input_attributes() {
    AudioInputAttributes attrs;
    attrs.samplerateItem = get_audio_attribute(AUDIO_INPUT, "sample_rate");
    attrs.bitwidthItem   = get_audio_attribute(AUDIO_INPUT, "bitwidth");
    attrs.soundmodeItem  = get_audio_attribute(AUDIO_INPUT, "soundmode");
    attrs.frmNumItem     = get_audio_attribute(AUDIO_INPUT, "frmNum");
    attrs.chnCntItem     = get_audio_attribute(AUDIO_INPUT, "chnCnt");
    attrs.SetVolItem     = get_audio_attribute(AUDIO_INPUT, "SetVol");
    attrs.SetGainItem    = get_audio_attribute(AUDIO_INPUT, "SetGain");
    attrs.usrFrmDepthItem= get_audio_attribute(AUDIO_INPUT, "usrFrmDepth");
    return attrs;
}
