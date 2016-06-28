/* Copyright 2016 OpenMarket Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "olm/inbound_group_session.h"

#include <string.h>

#include "olm/base64.h"
#include "olm/cipher.h"
#include "olm/error.h"
#include "olm/megolm.h"
#include "olm/memory.h"
#include "olm/message.h"
#include "olm/pickle.h"
#include "olm/pickle_encoding.h"


#define OLM_PROTOCOL_VERSION     3
#define PICKLE_VERSION           1

struct OlmInboundGroupSession {
    /** our earliest known ratchet value */
    Megolm initial_ratchet;

    /** The most recent ratchet value */
    Megolm latest_ratchet;

    enum OlmErrorCode last_error;
};

size_t olm_inbound_group_session_size() {
    return sizeof(OlmInboundGroupSession);
}

OlmInboundGroupSession * olm_inbound_group_session(
    void *memory
) {
    OlmInboundGroupSession *session = memory;
    olm_clear_inbound_group_session(session);
    return session;
}

const char *olm_inbound_group_session_last_error(
    const OlmInboundGroupSession *session
) {
    return _olm_error_to_string(session->last_error);
}

size_t olm_clear_inbound_group_session(
    OlmInboundGroupSession *session
) {
    _olm_unset(session, sizeof(OlmInboundGroupSession));
    return sizeof(OlmInboundGroupSession);
}

size_t olm_init_inbound_group_session(
    OlmInboundGroupSession *session,
    uint32_t message_index,
    const uint8_t * session_key, size_t session_key_length
) {
    uint8_t key_buf[MEGOLM_RATCHET_LENGTH];
    size_t raw_length = _olm_decode_base64_length(session_key_length);

    if (raw_length == (size_t)-1) {
        session->last_error = OLM_INVALID_BASE64;
        return (size_t)-1;
    }

    if (raw_length != MEGOLM_RATCHET_LENGTH) {
        session->last_error = OLM_BAD_SESSION_KEY;
        return (size_t)-1;
    }

    _olm_decode_base64(session_key, session_key_length, key_buf);
    megolm_init(&session->initial_ratchet, key_buf, message_index);
    megolm_init(&session->latest_ratchet, key_buf, message_index);
    _olm_unset(key_buf, MEGOLM_RATCHET_LENGTH);

    return 0;
}

static size_t raw_pickle_length(
    const OlmInboundGroupSession *session
) {
    size_t length = 0;
    length += _olm_pickle_uint32_length(PICKLE_VERSION);
    length += megolm_pickle_length(&session->initial_ratchet);
    length += megolm_pickle_length(&session->latest_ratchet);
    return length;
}

size_t olm_pickle_inbound_group_session_length(
    const OlmInboundGroupSession *session
) {
    return _olm_enc_output_length(raw_pickle_length(session));
}

size_t olm_pickle_inbound_group_session(
    OlmInboundGroupSession *session,
    void const * key, size_t key_length,
    void * pickled, size_t pickled_length
) {
    size_t raw_length = raw_pickle_length(session);
    uint8_t *pos;

    if (pickled_length < _olm_enc_output_length(raw_length)) {
        session->last_error = OLM_OUTPUT_BUFFER_TOO_SMALL;
        return (size_t)-1;
    }

    pos = _olm_enc_output_pos(pickled, raw_length);
    pos = _olm_pickle_uint32(pos, PICKLE_VERSION);
    pos = megolm_pickle(&session->initial_ratchet, pos);
    pos = megolm_pickle(&session->latest_ratchet, pos);

    return _olm_enc_output(key, key_length, pickled, raw_length);
}

size_t olm_unpickle_inbound_group_session(
    OlmInboundGroupSession *session,
    void const * key, size_t key_length,
    void * pickled, size_t pickled_length
) {
    const uint8_t *pos;
    const uint8_t *end;
    uint32_t pickle_version;

    size_t raw_length = _olm_enc_input(
        key, key_length, pickled, pickled_length, &(session->last_error)
    );
    if (raw_length == (size_t)-1) {
        return raw_length;
    }

    pos = pickled;
    end = pos + raw_length;
    pos = _olm_unpickle_uint32(pos, end, &pickle_version);
    if (pickle_version != PICKLE_VERSION) {
        session->last_error = OLM_UNKNOWN_PICKLE_VERSION;
        return (size_t)-1;
    }
    pos = megolm_unpickle(&session->initial_ratchet, pos, end);
    pos = megolm_unpickle(&session->latest_ratchet, pos, end);

    if (end != pos) {
        /* We had the wrong number of bytes in the input. */
        session->last_error = OLM_CORRUPTED_PICKLE;
        return (size_t)-1;
    }

    return pickled_length;
}

/**
 * get the max plaintext length in an un-base64-ed message
 */
static size_t _decrypt_max_plaintext_length(
    OlmInboundGroupSession *session,
    uint8_t * message, size_t message_length
) {
    struct _OlmDecodeGroupMessageResults decoded_results;

    _olm_decode_group_message(
        message, message_length,
        megolm_cipher->ops->mac_length(megolm_cipher),
        &decoded_results);

    if (decoded_results.version != OLM_PROTOCOL_VERSION) {
        session->last_error = OLM_BAD_MESSAGE_VERSION;
        return (size_t)-1;
    }

    if (!decoded_results.ciphertext) {
        session->last_error = OLM_BAD_MESSAGE_FORMAT;
        return (size_t)-1;
    }

    return megolm_cipher->ops->decrypt_max_plaintext_length(
        megolm_cipher, decoded_results.ciphertext_length);
}

size_t olm_group_decrypt_max_plaintext_length(
    OlmInboundGroupSession *session,
    uint8_t * message, size_t message_length
) {
    size_t raw_length;

    raw_length = _olm_decode_base64(message, message_length, message);
    if (raw_length == (size_t)-1) {
        session->last_error = OLM_INVALID_BASE64;
        return (size_t)-1;
    }

    return _decrypt_max_plaintext_length(
        session, message, raw_length
    );
}

/**
 * decrypt an un-base64-ed message
 */
static size_t _decrypt(
    OlmInboundGroupSession *session,
    uint8_t * message, size_t message_length,
    uint8_t * plaintext, size_t max_plaintext_length
) {
    struct _OlmDecodeGroupMessageResults decoded_results;
    size_t max_length, r;
    Megolm *megolm;
    Megolm tmp_megolm;

    _olm_decode_group_message(
        message, message_length,
        megolm_cipher->ops->mac_length(megolm_cipher),
        &decoded_results);

    if (decoded_results.version != OLM_PROTOCOL_VERSION) {
        session->last_error = OLM_BAD_MESSAGE_VERSION;
        return (size_t)-1;
    }

    if (!decoded_results.has_message_index || !decoded_results.session_id
        || !decoded_results.ciphertext
    ) {
        session->last_error = OLM_BAD_MESSAGE_FORMAT;
        return (size_t)-1;
    }

    max_length = megolm_cipher->ops->decrypt_max_plaintext_length(
        megolm_cipher,
        decoded_results.ciphertext_length
    );
    if (max_plaintext_length < max_length) {
        session->last_error = OLM_OUTPUT_BUFFER_TOO_SMALL;
        return (size_t)-1;
    }

    /* pick a megolm instance to use. If we're at or beyond the latest ratchet
     * value, use that */
    if ((decoded_results.message_index - session->latest_ratchet.counter) < (1U << 31)) {
        megolm = &session->latest_ratchet;
    } else if ((decoded_results.message_index - session->initial_ratchet.counter) >= (1U << 31)) {
        /* the counter is before our intial ratchet - we can't decode this. */
        session->last_error = OLM_UNKNOWN_MESSAGE_INDEX;
        return (size_t)-1;
    } else {
        /* otherwise, start from the initial megolm. Take a copy so that we
         * don't overwrite the initial megolm */
        tmp_megolm = session->initial_ratchet;
        megolm = &tmp_megolm;
    }

    megolm_advance_to(megolm, decoded_results.message_index);

    /* now try checking the mac, and decrypting */
    r = megolm_cipher->ops->decrypt(
        megolm_cipher,
        megolm_get_data(megolm), MEGOLM_RATCHET_LENGTH,
        message, message_length,
        decoded_results.ciphertext, decoded_results.ciphertext_length,
        plaintext, max_plaintext_length
    );

    _olm_unset(&tmp_megolm, sizeof(tmp_megolm));
    if (r == (size_t)-1) {
        session->last_error = OLM_BAD_MESSAGE_MAC;
        return r;
    }

    return r;
}

size_t olm_group_decrypt(
    OlmInboundGroupSession *session,
    uint8_t * message, size_t message_length,
    uint8_t * plaintext, size_t max_plaintext_length
) {
    size_t raw_message_length;

    raw_message_length = _olm_decode_base64(message, message_length, message);
    if (raw_message_length == (size_t)-1) {
        session->last_error = OLM_INVALID_BASE64;
        return (size_t)-1;
    }

    return _decrypt(
        session, message, raw_message_length,
        plaintext, max_plaintext_length
    );
}
