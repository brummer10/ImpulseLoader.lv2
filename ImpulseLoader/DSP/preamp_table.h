
#include "preamp_data.cc"

struct PreEntry {
    PreDesc *data;
} pre_table[] = {
    { &static_cast<PreDesc&>(pre_data_ac30) },
    { &static_cast<PreDesc&>(pre_data_bassman) },
    { &static_cast<PreDesc&>(pre_data_tube) },
    { &static_cast<PreDesc&>(pre_data_fender) },
    { &static_cast<PreDesc&>(pre_data_jcm800) },
    { &static_cast<PreDesc&>(pre_data_jtm45) },
    { &static_cast<PreDesc&>(pre_data_mesaboogie) },
    { &static_cast<PreDesc&>(pre_data_boutique) },
    { &static_cast<PreDesc&>(pre_data_ampeg) },
    { &static_cast<PreDesc&>(pre_data_rectifier) },
};
static const unsigned int pre_table_size = sizeof(pre_table) / sizeof(pre_table[0]);

static PreEntry& getPreEntry(unsigned int n) {
    if (n >= pre_table_size) {
        n = pre_table_size - 1;
    }
    return pre_table[n];
}
