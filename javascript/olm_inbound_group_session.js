/* The 'length' argument to Pointer_stringify doesn't work if the input includes
 * characters >= 128; we therefore need to add a NULL character to all of our
 * strings. This acts as a symbolic constant to help show what we're doing.
 */
var NULL_BYTE_PADDING_LENGTH = 1;

function InboundGroupSession() {
    var size = Module['_olm_inbound_group_session_size']();
    this.buf = malloc(size);
    this.ptr = Module['_olm_inbound_group_session'](this.buf);
}

function inbound_group_session_method(wrapped) {
    return function() {
        var result = wrapped.apply(this, arguments);
        if (result === OLM_ERROR) {
            var message = Pointer_stringify(
                Module['_olm_inbound_group_session_last_error'](arguments[0])
            );
            throw new Error("OLM." + message);
        }
        return result;
    }
}

InboundGroupSession.prototype['free'] = function() {
    Module['_olm_clear_inbound_group_session'](this.ptr);
    free(this.ptr);
}

InboundGroupSession.prototype['pickle'] = restore_stack(function(key) {
    var key_array = array_from_string(key);
    var pickle_length = inbound_group_session_method(
        Module['_olm_pickle_inbound_group_session_length']
    )(this.ptr);
    var key_buffer = stack(key_array);
    var pickle_buffer = stack(pickle_length + NULL_BYTE_PADDING_LENGTH);
    inbound_group_session_method(Module['_olm_pickle_inbound_group_session'])(
        this.ptr, key_buffer, key_array.length, pickle_buffer, pickle_length
    );
    return Pointer_stringify(pickle_buffer);
});

InboundGroupSession.prototype['unpickle'] = restore_stack(function(key, pickle) {
    var key_array = array_from_string(key);
    var key_buffer = stack(key_array);
    var pickle_array = array_from_string(pickle);
    var pickle_buffer = stack(pickle_array);
    inbound_group_session_method(Module['_olm_unpickle_inbound_group_session'])(
        this.ptr, key_buffer, key_array.length, pickle_buffer,
        pickle_array.length
    );
});

InboundGroupSession.prototype['create'] = restore_stack(function(session_key) {
    var key_array = array_from_string(session_key);
    var key_buffer = stack(key_array);

    inbound_group_session_method(Module['_olm_init_inbound_group_session'])(
        this.ptr, key_buffer, key_array.length
    );
});

InboundGroupSession.prototype['decrypt'] = restore_stack(function(
    message
) {
    var message_array = array_from_string(message);
    var message_buffer = stack(message_array);
    var max_plaintext_length = inbound_group_session_method(
        Module['_olm_group_decrypt_max_plaintext_length']
    )(this.ptr, message_buffer, message_array.length);
    // caculating the length destroys the input buffer.
    // So we copy the array to a new buffer
    var message_buffer = stack(message_array);
    var plaintext_buffer = stack(max_plaintext_length + NULL_BYTE_PADDING_LENGTH);
    var plaintext_length = inbound_group_session_method(Module["_olm_group_decrypt"])(
        this.ptr,
        message_buffer, message_array.length,
        plaintext_buffer, max_plaintext_length
    );

    // Pointer_stringify requires a null-terminated argument (the optional
    // 'len' argument doesn't work for UTF-8 data).
    Module['setValue'](
        plaintext_buffer+plaintext_length,
        0, "i8"
    );

    return Pointer_stringify(plaintext_buffer);
});

InboundGroupSession.prototype['session_id'] = restore_stack(function() {
    var length = inbound_group_session_method(
        Module['_olm_inbound_group_session_id_length']
    )(this.ptr);
    var session_id = stack(length + NULL_BYTE_PADDING_LENGTH);
    inbound_group_session_method(Module['_olm_inbound_group_session_id'])(
        this.ptr, session_id, length
    );
    return Pointer_stringify(session_id);
});

olm_exports['InboundGroupSession'] = InboundGroupSession;
