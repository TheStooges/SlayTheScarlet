/* A run's record as the score screen shows it: each match and what its reward changed. */
#include <stdio.h>
#include <string.h>
#include "internal.h"

void count_results(const sts_run *run, int *won, int *lost, int *rounds_won, int *rounds_lost)
{
    int wau;

    *won = *lost = *rounds_won = *rounds_lost = 0;
    for (wau = 0; wau < run->history_count; wau++) {
        const sts_match_record *record = &run->history[wau];

        if (record->won)
            (*won)++;
        else
            (*lost)++;
        *rounds_won += record->rounds_won;
        *rounds_lost += record->rounds_lost;
    }
}

/* Card names, each once with its copies: "Silver Bound x2, Killing Doll". The IDs of one card are
 * together, as the run lists them. */
static void card_list(const uint16_t *ids, int count, char *out, size_t size)
{
    size_t used = 0;
    int wau = 0;

    out[0] = 0;
    while (wau < count) {
        const card_text *entry = find_text(ids[wau]);
        int copies = 1, written;

        while (wau + copies < count && ids[wau + copies] == ids[wau])
            copies++;
        if (copies > 1)
            written = _snprintf(out + used, size - used - 1, "%s%s x%d", used ? ", " : "", entry ? entry->name : "?", copies);
        else
            written = _snprintf(out + used, size - used - 1, "%s%s", used ? ", " : "", entry ? entry->name : "?");
        if (written < 0)
            break;
        used += (size_t)written;
        wau += copies;
    }
    out[size - 1] = 0;
}

void describe_match(const sts_run *run, int index, char *result, size_t result_size, char *change, size_t change_size)
{
    const sts_match_record *record = &run->history[index];
    int number = run->history_forgotten + index + 1;
    int final_match = run->phase == STS_PHASE_FINISHED && index == run->history_count - 1;
    char added[256], removed[256];

    if (final_match && record->opponent >= 0)
        _snprintf(result, result_size - 1, record->won ? tr("Final match: won %d-%d against %s") : tr("Final match: lost %d-%d against %s"),
                  record->rounds_won, record->rounds_lost, character_label(record->opponent));
    else if (final_match)
        _snprintf(result, result_size - 1, record->won ? tr("Final match: won %d-%d") : tr("Final match: lost %d-%d"),
                  record->rounds_won, record->rounds_lost);
    else if (record->opponent >= 0)
        _snprintf(result, result_size - 1, record->won ? tr("Match %d: won %d-%d against %s") : tr("Match %d: lost %d-%d against %s"),
                  number, record->rounds_won, record->rounds_lost, character_label(record->opponent));
    else
        _snprintf(result, result_size - 1, record->won ? tr("Match %d: won %d-%d") : tr("Match %d: lost %d-%d"),
                  number, record->rounds_won, record->rounds_lost);
    result[result_size - 1] = 0;

    card_list(record->added, record->added_count, added, sizeof added);
    card_list(record->removed, record->removed_count, removed, sizeof removed);
    if (record->pending)
        _snprintf(change, change_size - 1, "%s", tr("Reward still to come"));
    else if (final_match)
        change[0] = 0;   /* no offer, which goes without saying */
    /* What left is shown as cards on the score screen, so the line only names what came in. */
    else if (record->added_count)
        _snprintf(change, change_size - 1, tr("In: %s"), added);
    else if (record->removed_count)
        _snprintf(change, change_size - 1, tr("Out: %s"), removed);
    else
        _snprintf(change, change_size - 1, "%s", tr("No change"));
    change[change_size - 1] = 0;
}
