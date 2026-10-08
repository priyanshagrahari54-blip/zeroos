#include <zeroos/desktop/nlp.h>
#include <string.h>

/* --- phrase table --------------------------------------------------
 * Ordered so that a longer phrase beats a shorter one covering the same
 * words ("update kar do" beats a bare "karo" if one were ever added).
 * Matching is done on the normalised utterance with word-boundary checks,
 * so "format" can never match the negation "mat".
 */
struct nlp_phrase {
	const char *text;
	enum zd_nlp_intent_kind kind;
	uint32_t weight;
};

static const struct nlp_phrase nlp_phrases[] = {
	/* update / upgrade */
	{ "update kar do", ZD_NLP_INTENT_UPDATE, 750 },
	{ "update karo", ZD_NLP_INTENT_UPDATE, 750 },
	{ "update karde", ZD_NLP_INTENT_UPDATE, 750 },
	{ "upgrade kar do", ZD_NLP_INTENT_UPDATE, 750 },
	{ "upgrade karo", ZD_NLP_INTENT_UPDATE, 750 },
	{ "patch kar do", ZD_NLP_INTENT_UPDATE, 750 },
	{ "naya version", ZD_NLP_INTENT_UPDATE, 700 },
	{ "update", ZD_NLP_INTENT_UPDATE, 700 },
	{ "upgrade", ZD_NLP_INTENT_UPDATE, 700 },
	{ "patch", ZD_NLP_INTENT_UPDATE, 700 },
	/* install */
	{ "install kar do", ZD_NLP_INTENT_INSTALL, 750 },
	{ "install karo", ZD_NLP_INTENT_INSTALL, 750 },
	{ "daal do", ZD_NLP_INTENT_INSTALL, 700 },
	{ "dal do", ZD_NLP_INTENT_INSTALL, 700 },
	{ "setup karo", ZD_NLP_INTENT_INSTALL, 700 },
	{ "add karo", ZD_NLP_INTENT_INSTALL, 700 },
	{ "install", ZD_NLP_INTENT_INSTALL, 700 },
	/* uninstall */
	{ "uninstall kar do", ZD_NLP_INTENT_UNINSTALL, 750 },
	{ "hata do", ZD_NLP_INTENT_UNINSTALL, 700 },
	{ "hatao", ZD_NLP_INTENT_UNINSTALL, 700 },
	{ "mita do", ZD_NLP_INTENT_UNINSTALL, 700 },
	{ "delete karo", ZD_NLP_INTENT_UNINSTALL, 700 },
	{ "uninstall", ZD_NLP_INTENT_UNINSTALL, 700 },
	{ "remove", ZD_NLP_INTENT_UNINSTALL, 700 },
	/* launch */
	{ "chalu karo", ZD_NLP_INTENT_LAUNCH, 750 },
	{ "chalao", ZD_NLP_INTENT_LAUNCH, 700 },
	{ "kholo", ZD_NLP_INTENT_LAUNCH, 700 },
	{ "run karo", ZD_NLP_INTENT_LAUNCH, 700 },
	{ "launch", ZD_NLP_INTENT_LAUNCH, 700 },
	{ "open", ZD_NLP_INTENT_LAUNCH, 700 },
	/* terminate (may be promoted to POWER_OFF, see below) */
	{ "band kar do", ZD_NLP_INTENT_TERMINATE, 750 },
	{ "band karo", ZD_NLP_INTENT_TERMINATE, 750 },
	{ "bandh karo", ZD_NLP_INTENT_TERMINATE, 750 },
	{ "band karna", ZD_NLP_INTENT_TERMINATE, 700 },
	{ "kill", ZD_NLP_INTENT_TERMINATE, 700 },
	{ "stop", ZD_NLP_INTENT_TERMINATE, 700 },
	{ "close", ZD_NLP_INTENT_TERMINATE, 700 },
	/* power */
	{ "shutdown kar do", ZD_NLP_INTENT_POWER_OFF, 800 },
	{ "shutdown karo", ZD_NLP_INTENT_POWER_OFF, 800 },
	{ "power off", ZD_NLP_INTENT_POWER_OFF, 800 },
	{ "shutdown", ZD_NLP_INTENT_POWER_OFF, 800 },
	{ "restart karo", ZD_NLP_INTENT_REBOOT, 800 },
	{ "reboot karo", ZD_NLP_INTENT_REBOOT, 800 },
	{ "dobara chalu", ZD_NLP_INTENT_REBOOT, 750 },
	{ "restart", ZD_NLP_INTENT_REBOOT, 800 },
	{ "reboot", ZD_NLP_INTENT_REBOOT, 800 },
	/* backup / restore */
	{ "backup karo", ZD_NLP_INTENT_BACKUP, 750 },
	{ "backup lo", ZD_NLP_INTENT_BACKUP, 750 },
	{ "backup", ZD_NLP_INTENT_BACKUP, 700 },
	{ "restore karo", ZD_NLP_INTENT_RESTORE, 750 },
	{ "wapas laao", ZD_NLP_INTENT_RESTORE, 700 },
	{ "recover karo", ZD_NLP_INTENT_RESTORE, 700 },
	{ "restore", ZD_NLP_INTENT_RESTORE, 700 },
	/* search */
	{ "dhundo", ZD_NLP_INTENT_SEARCH, 700 },
	{ "dhoondo", ZD_NLP_INTENT_SEARCH, 700 },
	{ "khojo", ZD_NLP_INTENT_SEARCH, 700 },
	{ "search", ZD_NLP_INTENT_SEARCH, 700 },
	{ "find", ZD_NLP_INTENT_SEARCH, 700 },
};

/* Nouns the engine knows. Longest match wins so "bluetooth" is not
 * truncated to a shorter noun sharing a prefix. */
static const char *const nlp_targets[] = {
	"zeroos", "bluetooth", "desktop", "browser", "firefox", "kernel",
	"driver", "package", "system", "laptop", "machine", "computer",
	"office", "terminal", "display", "storage", "network", "theme",
	"fonts", "media", "audio", "video", "games", "wifi", "apps",
	"files", "notes", "vault", "os", "pc"
};

/* Targets that mean "the whole machine": "band karo" with one of these is a
 * shutdown, not a process kill. */
static const char *const nlp_system_targets[] = {
	"zeroos", "system", "laptop", "machine", "computer", "os", "pc"
};

static const char *const nlp_negations[] = {
	"nahi", "mat", "not", "never", "dont", "don"
};

static int nlp_is_lower_alnum(char c) {
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

uint32_t zd_nlp_normalize(char *text) {
	uint32_t words = 0;
	uint32_t out = 0;
	uint32_t i;
	int pending_space = 0;

	if (!text)
		return 0;
	for (i = 0; text[i]; ++i) {
		char c = text[i];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (nlp_is_lower_alnum(c)) {
			if (pending_space && out > 0)
				text[out++] = ' ';
			pending_space = 0;
			text[out++] = c;
		} else {
			pending_space = 1;
		}
	}
	text[out] = '\0';

	for (i = 0; i < out; ++i)
		if (text[i] == ' ' && (i == 0 || text[i - 1] != ' '))
			words++;
	if (out > 0 && text[0] != ' ')
		words++;
	return words;
}

/* Word-boundary substring test on already-normalised text (single spaces,
 * no punctuation), which makes it correct for multi-word phrases too. */
static int nlp_has_word(const char *hay, const char *word) {
	size_t wlen = strlen(word);
	const char *p = hay;

	if (!wlen)
		return 0;
	while ((p = strstr(p, word)) != NULL) {
		const char *after = p + wlen;
		int left_ok = (p == hay) || (*(p - 1) == ' ');
		int right_ok = (*after == '\0') || (*after == ' ');

		if (left_ok && right_ok)
			return 1;
		p = after;
	}
	return 0;
}

static void nlp_copy_slot(char *dst, uint32_t cap, const char *src) {
	uint32_t i = 0;

	if (!dst || !cap)
		return;
	if (src)
		for (; i + 1 < cap && src[i]; ++i)
			dst[i] = src[i];
	dst[i] = '\0';
}

/* Pull the first version-looking token out of the *raw* utterance: the
 * normaliser turns "2.4.1" into "2 4 1", so this has to run before it. */
static int nlp_extract_version(const char *raw, char *out, uint32_t cap) {
	uint32_t i = 0;
	uint32_t len = 0;
	int digits = 0;

	if (!raw || !out || !cap)
		return 0;
	out[0] = '\0';
	for (; raw[i]; ++i) {
		if (raw[i] >= '0' && raw[i] <= '9') {
			uint32_t start = i;

			len = 0;
			digits = 0;
			while (raw[i] && ((raw[i] >= '0' && raw[i] <= '9') ||
					  raw[i] == '.')) {
				if (raw[i] >= '0' && raw[i] <= '9')
					digits++;
				if (len + 1 < cap)
					out[len++] = raw[i];
				i++;
			}
			/* A trailing dot is punctuation, not part of a version. */
			while (len > 0 && out[len - 1] == '.')
				len--;
			out[len] = '\0';
			if (digits > 0 && len > 0) {
				(void)start;
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

static void nlp_requirements(enum zd_nlp_intent_kind kind,
			     uint32_t *context, uint32_t *actions) {
	*context = 0;
	*actions = 0;
	switch (kind) {
	case ZD_NLP_INTENT_UPDATE:
		*context = ZD_AI_GRANT_CONTEXT_SETTINGS;
		*actions = ZD_AI_GRANT_ACTION_FILE_WRITE;
		break;
	case ZD_NLP_INTENT_INSTALL:
		*context = ZD_AI_GRANT_CONTEXT_SETTINGS;
		*actions = ZD_AI_GRANT_ACTION_FILE_WRITE |
			   ZD_AI_GRANT_ACTION_NETWORK;
		break;
	case ZD_NLP_INTENT_UNINSTALL:
		*context = ZD_AI_GRANT_CONTEXT_SETTINGS;
		*actions = ZD_AI_GRANT_ACTION_FILE_WRITE |
			   ZD_AI_GRANT_ACTION_DESTRUCTIVE;
		break;
	case ZD_NLP_INTENT_LAUNCH:
		*actions = ZD_AI_GRANT_ACTION_PROCESS_EXEC;
		break;
	case ZD_NLP_INTENT_TERMINATE:
		*actions = ZD_AI_GRANT_ACTION_PROCESS_EXEC |
			   ZD_AI_GRANT_ACTION_DESTRUCTIVE;
		break;
	case ZD_NLP_INTENT_BACKUP:
		*context = ZD_AI_GRANT_CONTEXT_FILES;
		*actions = ZD_AI_GRANT_ACTION_FILE_READ |
			   ZD_AI_GRANT_ACTION_FILE_WRITE;
		break;
	case ZD_NLP_INTENT_RESTORE:
		*context = ZD_AI_GRANT_CONTEXT_FILES;
		*actions = ZD_AI_GRANT_ACTION_FILE_WRITE |
			   ZD_AI_GRANT_ACTION_DESTRUCTIVE;
		break;
	case ZD_NLP_INTENT_SEARCH:
		*context = ZD_AI_GRANT_CONTEXT_FILES;
		*actions = ZD_AI_GRANT_ACTION_FILE_READ;
		break;
	case ZD_NLP_INTENT_POWER_OFF:
	case ZD_NLP_INTENT_REBOOT:
		*actions = ZD_AI_GRANT_ACTION_DESTRUCTIVE;
		break;
	default:
		break;
	}
}

int zd_nlp_parse(const char *utterance, struct zd_nlp_intent *out) {
	char norm[ZD_NLP_TEXT_MAX];
	const struct nlp_phrase *best = NULL;
	const char *best_target = NULL;
	uint32_t best_len = 0;
	uint32_t target_len = 0;
	uint32_t confidence;
	uint32_t i;
	size_t copy;

	if (!utterance || !out)
		return -ZD_EINVAL;
	memset(out, 0, sizeof(*out));
	out->kind = ZD_NLP_INTENT_UNKNOWN;

	copy = strlen(utterance);
	if (copy >= sizeof(norm))
		copy = sizeof(norm) - 1;
	memcpy(norm, utterance, copy);
	norm[copy] = '\0';

	/* The version comes from the raw text: normalising destroys the dots. */
	nlp_extract_version(utterance, out->version,
			    (uint32_t)sizeof(out->version));

	out->tokens = zd_nlp_normalize(norm);

	for (i = 0; i < sizeof(nlp_phrases) / sizeof(nlp_phrases[0]); ++i) {
		uint32_t len = (uint32_t)strlen(nlp_phrases[i].text);

		if (!nlp_has_word(norm, nlp_phrases[i].text))
			continue;
		if (len > best_len) {
			best_len = len;
			best = &nlp_phrases[i];
		}
	}
	if (!best)
		return -ZD_ENOENT;
	out->kind = best->kind;

	for (i = 0; i < sizeof(nlp_targets) / sizeof(nlp_targets[0]); ++i) {
		uint32_t len = (uint32_t)strlen(nlp_targets[i]);

		if (!nlp_has_word(norm, nlp_targets[i]))
			continue;
		if (len > target_len) {
			target_len = len;
			best_target = nlp_targets[i];
		}
	}
	if (best_target)
		nlp_copy_slot(out->target, (uint32_t)sizeof(out->target),
			      best_target);

	/* "band karo" against the machine is a shutdown, not a process kill. */
	if (out->kind == ZD_NLP_INTENT_TERMINATE && best_target) {
		for (i = 0;
		     i < sizeof(nlp_system_targets) / sizeof(nlp_system_targets[0]);
		     ++i) {
			if (strcmp(best_target, nlp_system_targets[i]) == 0) {
				out->kind = ZD_NLP_INTENT_POWER_OFF;
				break;
			}
		}
	}

	for (i = 0; i < sizeof(nlp_negations) / sizeof(nlp_negations[0]); ++i)
		if (nlp_has_word(norm, nlp_negations[i])) {
			out->negated = 1;
			break;
		}

	confidence = best->weight;
	if (out->target[0])
		confidence += 200;
	if (out->version[0])
		confidence += 100;
	if (confidence > ZD_NLP_CONFIDENCE_MAX)
		confidence = ZD_NLP_CONFIDENCE_MAX;
	out->confidence = confidence;

	nlp_requirements(out->kind, &out->required_context,
			 &out->required_actions);
	return 0;
}

static uint32_t nlp_push(struct zd_nlp_plan *plan, enum zd_nlp_step step) {
	if (plan->step_count >= ZD_NLP_MAX_STEPS)
		return 0;
	plan->steps[plan->step_count++] = step;
	return 1;
}

int zd_nlp_plan(const struct zd_nlp_intent *intent, struct zd_nlp_plan *out) {
	uint32_t i;

	if (!intent || !out)
		return -ZD_EINVAL;
	if (intent->kind <= ZD_NLP_INTENT_UNKNOWN ||
	    intent->kind >= ZD_NLP_INTENT_COUNT)
		return -ZD_EINVAL;

	memset(out, 0, sizeof(*out));
	out->kind = intent->kind;
	out->confidence = intent->confidence;
	out->required_context = intent->required_context;
	out->required_actions = intent->required_actions;
	for (i = 0; i < sizeof(out->target); ++i)
		out->target[i] = intent->target[i];
	for (i = 0; i < sizeof(out->version); ++i)
		out->version[i] = intent->version[i];

	if (!nlp_push(out, ZD_NLP_STEP_PARSE))
		return -ZD_EOVERFLOW;

	switch (intent->kind) {
	case ZD_NLP_INTENT_POWER_OFF:
	case ZD_NLP_INTENT_REBOOT:
		break;
	default:
		if (!nlp_push(out, ZD_NLP_STEP_RESOLVE_TARGET))
			return -ZD_EOVERFLOW;
		break;
	}
	if (!nlp_push(out, ZD_NLP_STEP_CHECK_GRANTS))
		return -ZD_EOVERFLOW;

	switch (intent->kind) {
	case ZD_NLP_INTENT_UPDATE:
	case ZD_NLP_INTENT_INSTALL:
		if (!nlp_push(out, ZD_NLP_STEP_FETCH) ||
		    !nlp_push(out, ZD_NLP_STEP_STAGE) ||
		    !nlp_push(out, ZD_NLP_STEP_APPLY))
			return -ZD_EOVERFLOW;
		out->destructive = (intent->kind == ZD_NLP_INTENT_INSTALL) ? 0 : 1;
		break;
	case ZD_NLP_INTENT_UNINSTALL:
	case ZD_NLP_INTENT_RESTORE:
		if (!nlp_push(out, ZD_NLP_STEP_STAGE) ||
		    !nlp_push(out, ZD_NLP_STEP_APPLY))
			return -ZD_EOVERFLOW;
		out->destructive = 1;
		break;
	case ZD_NLP_INTENT_BACKUP:
		/* Read-mostly: it copies out, it does not remove anything. */
		if (!nlp_push(out, ZD_NLP_STEP_STAGE) ||
		    !nlp_push(out, ZD_NLP_STEP_APPLY))
			return -ZD_EOVERFLOW;
		break;
	case ZD_NLP_INTENT_LAUNCH:
	case ZD_NLP_INTENT_SEARCH:
		if (!nlp_push(out, ZD_NLP_STEP_APPLY))
			return -ZD_EOVERFLOW;
		break;
	case ZD_NLP_INTENT_TERMINATE:
	case ZD_NLP_INTENT_POWER_OFF:
	case ZD_NLP_INTENT_REBOOT:
		if (!nlp_push(out, ZD_NLP_STEP_APPLY))
			return -ZD_EOVERFLOW;
		out->destructive = 1;
		break;
	default:
		return -ZD_EINVAL;
	}
	return 0;
}

int zd_nlp_command(const char *utterance, uint32_t grants,
		   struct zd_nlp_plan *out) {
	struct zd_nlp_intent intent;
	int rc;

	if (!out)
		return -ZD_EINVAL;
	rc = zd_nlp_parse(utterance, &intent);
	if (rc != 0)
		return rc;
	if (intent.negated)
		return -ZD_ECANCELED;

	rc = zd_nlp_plan(&intent, out);
	if (rc != 0)
		return rc;
	if (intent.required_context & ~grants)
		return -ZD_EPERM;
	if (intent.required_actions & ~(grants & ZD_AI_GRANT_ACTION_ALL))
		return -ZD_EPERM;
	return 0;
}

int zd_nlp_drive_update(const struct zd_nlp_plan *plan, struct zd_update *u) {
	static const int lifecycle[] = {
		ZD_UPD_EV_DOWNLOAD_OK, ZD_UPD_EV_VERIFY_OK, ZD_UPD_EV_STAGE_OK,
		ZD_UPD_EV_PREFLIGHT_OK, ZD_UPD_EV_ACTIVATE_OK,
		ZD_UPD_EV_HEALTH_OK, ZD_UPD_EV_COMMIT_OK
	};
	uint32_t i;
	int rc;

	if (!plan || !u)
		return -ZD_EINVAL;
	if (plan->kind != ZD_NLP_INTENT_UPDATE &&
	    plan->kind != ZD_NLP_INTENT_INSTALL)
		return -ZD_EINVAL;
	/* No fabricated target: an update without a version is refused. */
	if (!plan->version[0])
		return -ZD_EINVAL;

	rc = zd_update_begin(u, plan->version);
	if (rc != 0)
		return rc;
	for (i = 0; i < sizeof(lifecycle) / sizeof(lifecycle[0]); ++i) {
		rc = zd_update_event(u, lifecycle[i]);
		if (rc != 0)
			return rc;
	}
	if (zd_update_state(u) != ZD_UPD_DONE)
		return -ZD_ESTATE;
	return 0;
}

const char *zd_nlp_intent_name(enum zd_nlp_intent_kind kind) {
	switch (kind) {
	case ZD_NLP_INTENT_UPDATE:    return "update";
	case ZD_NLP_INTENT_INSTALL:   return "install";
	case ZD_NLP_INTENT_UNINSTALL: return "uninstall";
	case ZD_NLP_INTENT_LAUNCH:    return "launch";
	case ZD_NLP_INTENT_TERMINATE: return "terminate";
	case ZD_NLP_INTENT_BACKUP:    return "backup";
	case ZD_NLP_INTENT_RESTORE:   return "restore";
	case ZD_NLP_INTENT_SEARCH:    return "search";
	case ZD_NLP_INTENT_POWER_OFF: return "power-off";
	case ZD_NLP_INTENT_REBOOT:    return "reboot";
	case ZD_NLP_INTENT_UNKNOWN:   return "unknown";
	default:                      return "invalid";
	}
}

const char *zd_nlp_step_name(enum zd_nlp_step step) {
	switch (step) {
	case ZD_NLP_STEP_PARSE:          return "parse";
	case ZD_NLP_STEP_RESOLVE_TARGET: return "resolve-target";
	case ZD_NLP_STEP_CHECK_GRANTS:   return "check-grants";
	case ZD_NLP_STEP_FETCH:          return "fetch";
	case ZD_NLP_STEP_STAGE:          return "stage";
	case ZD_NLP_STEP_APPLY:          return "apply";
	case ZD_NLP_STEP_REPORT:         return "report";
	case ZD_NLP_STEP_NONE:           return "none";
	default:                         return "invalid";
	}
}
