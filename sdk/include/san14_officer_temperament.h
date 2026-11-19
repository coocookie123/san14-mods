#ifndef SAN14_MOD_SDK_OFFICER_TEMPERAMENT_H
#define SAN14_MOD_SDK_OFFICER_TEMPERAMENT_H
#include "san14_types.h"

#define SAN14_TEMPERAMENT_HONOR        (1u << 0)
#define SAN14_TEMPERAMENT_AMBITION     (1u << 1)
#define SAN14_TEMPERAMENT_HAN_LOYALTY  (1u << 2)
#define SAN14_TEMPERAMENT_AGGRESSION   (1u << 3)
#define SAN14_TEMPERAMENT_DIPLOMACY    (1u << 4)
#define SAN14_TEMPERAMENT_RESOURCES    (1u << 5)
#define SAN14_TEMPERAMENT_CONSTRUCTION (1u << 6)
#define SAN14_TEMPERAMENT_ALL          ((1u << 7) - 1u)

/* Signed native weights, independent of the build's memory layout.
 * The usual five levels are -20, -10, 0, 10, 20. Resource preferences
 * contain three independent weights and are read or edited as one field. */
typedef struct San14OfficerTemperament {
    int16_t honor, ambition, han_loyalty, aggression, diplomacy, construction;
    int16_t gold_weight, food_weight, troops_weight;
} San14OfficerTemperament;

typedef struct San14OfficerTemperamentState {
    uint32_t struct_size, officer_id;
    uint64_t generation;
    San14OfficerTemperament base;       /* Values used by native serialization. */
    San14OfficerTemperament effective;  /* Values currently used by the game. */
} San14OfficerTemperamentState;

typedef struct San14OfficerTemperamentEdit {
    uint32_t struct_size, officer_id;
    uint64_t generation; /* From officer_temperament.read on this officer. */
    uint32_t field_mask;
    San14OfficerTemperament expected_base, values;
} San14OfficerTemperamentEdit;

typedef struct San14OfficerTemperamentOverride {
    uint32_t struct_size, field_mask;
    San14OfficerTemperament values;
} San14OfficerTemperamentOverride;

typedef struct San14OfficerTemperamentApi {
    uint32_t struct_size;
    /* UI thread, stable active world, game.read.officers. Set out->struct_size. */
    San14Result (SAN14_MOD_CALL *read)(void*,uint32_t,San14OfficerTemperamentState*);
    /* Also requires game.commands. Checks generation and selected expected
     * base values before applying atomically. Saving is a separate game action.
     * Temporarily overridden fields keep their effective values while editing
     * the underlying save values. */
    San14Result (SAN14_MOD_CALL *edit)(void*,const San14OfficerTemperamentEdit*);
    /* UI thread at main menu, game.rules, one owner. Copies the supplied
     * template. NULL releases it. Applies to all valid records on each
     * subsequent new game/load and does not serialize the overlay. */
    San14Result (SAN14_MOD_CALL *set_uniform_override)(void*,const San14OfficerTemperamentOverride*);
} San14OfficerTemperamentApi;
#endif
