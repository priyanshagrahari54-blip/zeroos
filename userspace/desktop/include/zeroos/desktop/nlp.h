#ifndef ZEROOS_DESKTOP_NLP_H
#define ZEROOS_DESKTOP_NLP_H
/* ZEROOS local intent engine (Stage 9 part F, "natural language -> action").
 *
 * The AI broker in ai.h routes requests to an *injected* backend; before this
 * module existed nothing in the tree could turn an utterance into an intent,
 * so ZD_AI_REQ_COMMAND had no local implementation at all. This is that
 * implementation.
 *
 * What it is: a deterministic lexicon-driven parser. It normalises an
 * utterance, matches it against a fixed phrase table (English and Hinglish),
 * extracts a target noun and a version slot, and produces an ordered,
 * permission-annotated action plan.
 *
 * What it is NOT: a learned model. It has no weights to train, no network
 * dependency and no notion of anything outside its table. An utterance that
 * matches nothing returns -ZD_ENOENT with confidence 0 and kind UNKNOWN; it
 * is never silently mapped to a default action. Confidence is derived from
 * which phrase matched and which slots were filled, so a caller can refuse
 * low-confidence plans instead of guessing.
 *
 * Pipeline:
 *   utterance -> zd_nlp_parse -> intent -> zd_nlp_plan -> plan
 *             -> zd_nlp_command (grants checked) -> zd_nlp_drive_update
 */
#include <zeroos/desktop/common.h>
#include <zeroos/desktop/ai.h>
#include <zeroos/desktop/update.h>

#define ZD_NLP_TEXT_MAX 160u
#define ZD_NLP_TARGET_MAX 32u
#define ZD_NLP_VERSION_MAX 16u
#define ZD_NLP_MAX_STEPS 6u

/* Confidence is 0..1000. Anything below ZD_NLP_CONFIDENCE_ACT is a guess
 * and callers should ask for confirmation rather than act. */
#define ZD_NLP_CONFIDENCE_MAX 1000u
#define ZD_NLP_CONFIDENCE_ACT 600u

enum zd_nlp_intent_kind {
	ZD_NLP_INTENT_UNKNOWN = 0,
	ZD_NLP_INTENT_UPDATE,
	ZD_NLP_INTENT_INSTALL,
	ZD_NLP_INTENT_UNINSTALL,
	ZD_NLP_INTENT_LAUNCH,
	ZD_NLP_INTENT_TERMINATE,
	ZD_NLP_INTENT_BACKUP,
	ZD_NLP_INTENT_RESTORE,
	ZD_NLP_INTENT_SEARCH,
	ZD_NLP_INTENT_POWER_OFF,
	ZD_NLP_INTENT_REBOOT,
	ZD_NLP_INTENT_COUNT
};

enum zd_nlp_step {
	ZD_NLP_STEP_NONE = 0,
	ZD_NLP_STEP_PARSE,
	ZD_NLP_STEP_RESOLVE_TARGET,
	ZD_NLP_STEP_CHECK_GRANTS,
	ZD_NLP_STEP_FETCH,
	ZD_NLP_STEP_STAGE,
	ZD_NLP_STEP_APPLY,
	ZD_NLP_STEP_REPORT
};

struct zd_nlp_intent {
	enum zd_nlp_intent_kind kind;
	char target[ZD_NLP_TARGET_MAX];
	char version[ZD_NLP_VERSION_MAX];
	uint32_t required_context;  /* ZD_AI_GRANT_CONTEXT_* bits */
	uint32_t required_actions;  /* ZD_AI_GRANT_ACTION_* bits */
	uint32_t confidence;        /* 0..ZD_NLP_CONFIDENCE_MAX */
	uint32_t tokens;            /* normalised word count */
	uint8_t negated;            /* "not"/"mat"/"nahi" present */
};

struct zd_nlp_plan {
	enum zd_nlp_intent_kind kind;
	enum zd_nlp_step steps[ZD_NLP_MAX_STEPS];
	uint32_t step_count;
	uint32_t required_context;
	uint32_t required_actions;
	uint32_t confidence;
	char target[ZD_NLP_TARGET_MAX];
	char version[ZD_NLP_VERSION_MAX];
	uint8_t destructive;
};

/* Normalise in place: lowercase ASCII, every other byte becomes a space,
 * runs of spaces collapsed, ends trimmed. Returns the word count. */
uint32_t zd_nlp_normalize(char *text);

/* Parse an utterance into an intent.
 *   0            an intent was recognised
 *   -ZD_EINVAL   null argument
 *   -ZD_ENOENT   nothing matched: out->kind stays UNKNOWN, confidence 0
 * A recognised intent always carries a non-zero confidence. */
int zd_nlp_parse(const char *utterance, struct zd_nlp_intent *out);

/* Turn an intent into an ordered plan. -ZD_EINVAL on null input or on
 * ZD_NLP_INTENT_UNKNOWN; -ZD_EOVERFLOW if the plan needs more steps than
 * ZD_NLP_MAX_STEPS (a table bug, surfaced rather than truncated). */
int zd_nlp_plan(const struct zd_nlp_intent *intent, struct zd_nlp_plan *out);

/* End-to-end utterance -> plan, gated by the grants the caller holds.
 *   -ZD_ENOENT  nothing matched
 *   -ZD_EPERM   the plan needs a grant that is not held
 * Negated utterances are refused (-ZD_ECANCELED) instead of executed. */
int zd_nlp_command(const char *utterance, uint32_t grants,
		   struct zd_nlp_plan *out);

/* Drive a full update lifecycle for a parsed plan: begin, download, verify,
 * stage, preflight, activate, health-check, commit. Requires an UPDATE or
 * INSTALL plan and an explicit version - a plan without a version is refused
 * (-ZD_EINVAL) rather than defaulted. Returns 0 with the update in
 * ZD_UPD_DONE, or the first negative error from the lifecycle. */
int zd_nlp_drive_update(const struct zd_nlp_plan *plan, struct zd_update *u);

const char *zd_nlp_intent_name(enum zd_nlp_intent_kind kind);
const char *zd_nlp_step_name(enum zd_nlp_step step);

#endif
