#include "test_harness.h"
#include <zeroos/desktop/nlp.h>

/* The engine must be able to go from a spoken sentence to a completed update
 * lifecycle with no injected backend anywhere in the path. These tests drive
 * that whole chain, including the refusal paths. */

static void nlp_normalize_behaves(void) {
	char buf[ZD_NLP_TEXT_MAX];
	uint32_t words;

	strcpy(buf, "  Update   the KERNEL, to 2.4.1!  ");
	words = zd_nlp_normalize(buf);
	ZD_CHECK(strcmp(buf, "update the kernel to 2 4 1") == 0);
	/* "update the kernel to 2 4 1" - the version's dots became spaces,
	 * so the version contributes three words, not one. */
	ZD_CHECK_EQ(words, 7);

	ZD_CHECK_EQ(zd_nlp_normalize(0), 0);
	buf[0] = 'x';
	buf[1] = 0;
	ZD_CHECK_EQ(zd_nlp_normalize(buf), 1);
}

static void nlp_parses_english_update(void) {
	struct zd_nlp_intent in;

	ZD_CHECK_OK(zd_nlp_parse("update the kernel to 2.4.1", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UPDATE);
	ZD_CHECK(strcmp(in.target, "kernel") == 0);
	ZD_CHECK(strcmp(in.version, "2.4.1") == 0);
	ZD_CHECK_EQ(in.confidence, ZD_NLP_CONFIDENCE_MAX);
	ZD_CHECK_EQ(in.negated, 0);
	ZD_CHECK(in.required_actions & ZD_AI_GRANT_ACTION_FILE_WRITE);
}

static void nlp_parses_hinglish_update(void) {
	struct zd_nlp_intent in;

	ZD_CHECK_OK(zd_nlp_parse("kernel ka update kar do 2.4.1", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UPDATE);
	ZD_CHECK(strcmp(in.target, "kernel") == 0);
	ZD_CHECK(strcmp(in.version, "2.4.1") == 0);
	/* 750 (phrase) + 200 (target) + 100 (version), capped. */
	ZD_CHECK_EQ(in.confidence, ZD_NLP_CONFIDENCE_MAX);
}

static void nlp_longest_phrase_wins(void) {
	struct zd_nlp_intent in;

	/* "update kar do" is the longer, more specific match. */
	ZD_CHECK_OK(zd_nlp_parse("zeroos update kar do 3.0", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UPDATE);
	ZD_CHECK(strcmp(in.target, "zeroos") == 0);
}

static void nlp_band_karo_disambiguates(void) {
	struct zd_nlp_intent in;

	/* Against the machine it is a shutdown... */
	ZD_CHECK_OK(zd_nlp_parse("system band karo", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_POWER_OFF);
	ZD_CHECK(strcmp(in.target, "system") == 0);

	/* ...against an app it is a process kill. */
	ZD_CHECK_OK(zd_nlp_parse("browser band karo", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_TERMINATE);
	ZD_CHECK(strcmp(in.target, "browser") == 0);

	ZD_CHECK_OK(zd_nlp_parse("shutdown karo", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_POWER_OFF);
	ZD_CHECK_EQ(in.confidence, 800u);
}

static void nlp_unknown_is_never_guessed(void) {
	struct zd_nlp_intent in;

	in.kind = ZD_NLP_INTENT_UPDATE;   /* must be overwritten */
	in.confidence = 12345;
	ZD_CHECK_ERR(zd_nlp_parse("aaj mausam kaisa hai", &in), ZD_ENOENT);
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UNKNOWN);
	ZD_CHECK_EQ(in.confidence, 0u);
	ZD_CHECK(in.target[0] == '\0');
}

static void nlp_word_boundaries_are_real(void) {
	struct zd_nlp_intent in;

	/* "format" contains "mat"; it must not read as a negation. */
	ZD_CHECK_OK(zd_nlp_parse("remove the format helper", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UNINSTALL);
	ZD_CHECK_EQ(in.negated, 0);
}

static void nlp_negation_refuses(void) {
	struct zd_nlp_intent in;
	struct zd_nlp_plan plan;

	ZD_CHECK_OK(zd_nlp_parse("kernel update mat karo 2.4.1", &in));
	ZD_CHECK_EQ(in.kind, ZD_NLP_INTENT_UPDATE);
	ZD_CHECK_EQ(in.negated, 1);
	ZD_CHECK_ERR(zd_nlp_command("kernel update mat karo 2.4.1",
				    ZD_AI_GRANT_ALL, &plan), ZD_ECANCELED);
}

static void nlp_command_gates_on_grants(void) {
	struct zd_nlp_plan plan;
	uint32_t need = ZD_AI_GRANT_CONTEXT_SETTINGS |
			ZD_AI_GRANT_ACTION_FILE_WRITE;

	/* No grants at all: refused before anything is planned away. */
	ZD_CHECK_ERR(zd_nlp_command("kernel update karo 2.4.1", 0, &plan),
		     ZD_EPERM);
	/* Context only, no action grant: still refused. */
	ZD_CHECK_ERR(zd_nlp_command("kernel update karo 2.4.1",
				    ZD_AI_GRANT_CONTEXT_SETTINGS, &plan),
		     ZD_EPERM);
	ZD_CHECK_OK(zd_nlp_command("kernel update karo 2.4.1", need, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_UPDATE);

	/* Install additionally needs network egress. */
	ZD_CHECK_ERR(zd_nlp_command("wifi driver install karo", need, &plan),
		     ZD_EPERM);
	ZD_CHECK_OK(zd_nlp_command("wifi driver install karo",
				   need | ZD_AI_GRANT_ACTION_NETWORK, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_INSTALL);
}

static void nlp_plans_are_ordered(void) {
	struct zd_nlp_intent in;
	struct zd_nlp_plan plan;

	ZD_CHECK_OK(zd_nlp_parse("update the kernel to 2.4.1", &in));
	ZD_CHECK_OK(zd_nlp_plan(&in, &plan));
	ZD_CHECK_EQ(plan.step_count, 6u);
	ZD_CHECK_EQ(plan.steps[0], ZD_NLP_STEP_PARSE);
	ZD_CHECK_EQ(plan.steps[1], ZD_NLP_STEP_RESOLVE_TARGET);
	ZD_CHECK_EQ(plan.steps[2], ZD_NLP_STEP_CHECK_GRANTS);
	ZD_CHECK_EQ(plan.steps[3], ZD_NLP_STEP_FETCH);
	ZD_CHECK_EQ(plan.steps[4], ZD_NLP_STEP_STAGE);
	ZD_CHECK_EQ(plan.steps[5], ZD_NLP_STEP_APPLY);

	/* Power intents have no target to resolve. */
	ZD_CHECK_OK(zd_nlp_parse("reboot karo", &in));
	ZD_CHECK_OK(zd_nlp_plan(&in, &plan));
	ZD_CHECK_EQ(plan.step_count, 3u);
	ZD_CHECK_EQ(plan.destructive, 1);

	/* UNKNOWN never produces a plan. */
	in.kind = ZD_NLP_INTENT_UNKNOWN;
	ZD_CHECK_ERR(zd_nlp_plan(&in, &plan), ZD_EINVAL);
	ZD_CHECK_ERR(zd_nlp_plan(0, &plan), ZD_EINVAL);
}

static void nlp_version_extraction(void) {
	struct zd_nlp_intent in;

	ZD_CHECK_OK(zd_nlp_parse("update to 1.10.2 now", &in));
	ZD_CHECK(strcmp(in.version, "1.10.2") == 0);

	/* A trailing period is punctuation, not part of the version. */
	ZD_CHECK_OK(zd_nlp_parse("kernel update karo 2.4.1.", &in));
	ZD_CHECK(strcmp(in.version, "2.4.1") == 0);

	/* No digits at all: empty slot, lower confidence, still an intent. */
	ZD_CHECK_OK(zd_nlp_parse("update", &in));
	ZD_CHECK(in.version[0] == '\0');
	ZD_CHECK_EQ(in.confidence, 700u);
}

static void nlp_end_to_end_update(void) {
	struct zd_nlp_plan plan;
	struct zd_update u;

	/* "kuch bhi bolu turant uska update ban jaye": one utterance in,
	 * a committed update out, with no injected backend in between. */
	ZD_CHECK_OK(zd_nlp_command("zeroos ka update kar do 4.2.0",
				   ZD_AI_GRANT_ALL, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_UPDATE);
	ZD_CHECK(strcmp(plan.version, "4.2.0") == 0);
	ZD_CHECK(strcmp(plan.target, "zeroos") == 0);

	zd_update_init(&u, 0);
	ZD_CHECK_OK(zd_nlp_drive_update(&plan, &u));
	ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_DONE);
	ZD_CHECK(strcmp(u.version, "4.2.0") == 0);
	ZD_CHECK_EQ(u.stats.started, 1u);
	ZD_CHECK_EQ(u.stats.committed, 1u);

	/* A second lifecycle on the same object starts from DONE, not busy. */
	ZD_CHECK_OK(zd_nlp_drive_update(&plan, &u));
	ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_DONE);
}

static void nlp_update_without_version_is_refused(void) {
	struct zd_nlp_plan plan;
	struct zd_update u;

	zd_update_init(&u, 0);
	ZD_CHECK_OK(zd_nlp_command("kernel update karo", ZD_AI_GRANT_ALL,
				   &plan));
	ZD_CHECK(plan.version[0] == '\0');
	/* No fabricated target version. */
	ZD_CHECK_ERR(zd_nlp_drive_update(&plan, &u), ZD_EINVAL);
	ZD_CHECK_EQ(zd_update_state(&u), ZD_UPD_IDLE);

	/* Only update/install plans may drive the lifecycle. */
	ZD_CHECK_OK(zd_nlp_command("browser kholo", ZD_AI_GRANT_ALL, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_LAUNCH);
	ZD_CHECK_ERR(zd_nlp_drive_update(&plan, &u), ZD_EINVAL);

	ZD_CHECK_ERR(zd_nlp_drive_update(0, &u), ZD_EINVAL);
	ZD_CHECK_ERR(zd_nlp_drive_update(&plan, 0), ZD_EINVAL);
}

static void nlp_destructive_intents_are_flagged(void) {
	struct zd_nlp_intent in;
	struct zd_nlp_plan plan;

	ZD_CHECK_OK(zd_nlp_parse("purana theme hata do", &in));
	ZD_CHECK_OK(zd_nlp_plan(&in, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_UNINSTALL);
	ZD_CHECK_EQ(plan.destructive, 1);
	ZD_CHECK(plan.required_actions & ZD_AI_GRANT_ACTION_DESTRUCTIVE);

	ZD_CHECK_OK(zd_nlp_parse("files ka backup lo", &in));
	ZD_CHECK_OK(zd_nlp_plan(&in, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_BACKUP);
	ZD_CHECK_EQ(plan.destructive, 0);

	ZD_CHECK_OK(zd_nlp_parse("notes dhundo", &in));
	ZD_CHECK_OK(zd_nlp_plan(&in, &plan));
	ZD_CHECK_EQ(plan.kind, ZD_NLP_INTENT_SEARCH);
	ZD_CHECK_EQ(plan.destructive, 0);
}

static void nlp_names_are_total(void) {
	int k;
	int s;

	for (k = 0; k < ZD_NLP_INTENT_COUNT; ++k)
		ZD_CHECK(zd_nlp_intent_name((enum zd_nlp_intent_kind)k)[0] != '\0');
	ZD_CHECK(strcmp(zd_nlp_intent_name((enum zd_nlp_intent_kind)999),
			"invalid") == 0);
	for (s = ZD_NLP_STEP_NONE; s <= ZD_NLP_STEP_REPORT; ++s)
		ZD_CHECK(zd_nlp_step_name((enum zd_nlp_step)s)[0] != '\0');
	ZD_CHECK(strcmp(zd_nlp_step_name((enum zd_nlp_step)999),
			"invalid") == 0);
}

void zd_test_nlp_suite(void) {
	ZD_RUN(nlp_normalize_behaves);
	ZD_RUN(nlp_parses_english_update);
	ZD_RUN(nlp_parses_hinglish_update);
	ZD_RUN(nlp_longest_phrase_wins);
	ZD_RUN(nlp_band_karo_disambiguates);
	ZD_RUN(nlp_unknown_is_never_guessed);
	ZD_RUN(nlp_word_boundaries_are_real);
	ZD_RUN(nlp_negation_refuses);
	ZD_RUN(nlp_command_gates_on_grants);
	ZD_RUN(nlp_plans_are_ordered);
	ZD_RUN(nlp_version_extraction);
	ZD_RUN(nlp_end_to_end_update);
	ZD_RUN(nlp_update_without_version_is_refused);
	ZD_RUN(nlp_destructive_intents_are_flagged);
	ZD_RUN(nlp_names_are_total);
}
