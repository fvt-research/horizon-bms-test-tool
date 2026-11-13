function on_start(ctx)
    return {
        {action = "log", message = "starting"},
        {action = "send", message_type = "start.immediate"},
        {action = "send", message_type = "start.delayed", delay_ms = 50},
    }
end

function on_message(msg)
    if msg.type == "bms.status" then
        return {
            action = "send",
            message_type = "bms.reaction",
            delay_ms = 25,
            fields = {
                observed = msg.fields.fault_active,
                voltage = msg.fields.voltage,
                label = msg.fields.label,
            },
        }
    elseif msg.type == "bms.failure" then
        return {action = "send", message_type = "send.failure"}
    elseif msg.type == "bms.delayed_failure" then
        return {action = "send", message_type = "send.failure", delay_ms = 5}
    elseif msg.type == "bms.invalid" then
        return {action = "unsupported"}
    elseif msg.type == "bms.partial_invalid" then
        return {
            {action = "send", message_type = "must.not.send"},
            {action = "unsupported"},
        }
    elseif msg.type == "bms.invalid_delay" then
        return {action = "send", message_type = "test", delay_ms = -1}
    elseif msg.type == "bms.invalid_fields" then
        return {action = "send", message_type = "test", fields = {x = {}}}
    elseif msg.type == "bms.sparse" then
        return {[2] = {action = "log", message = "unexpected"}}
    elseif msg.type == "bms.script_error" then
        error("fixture failure")
    elseif msg.type == "bms.metatable" then
        return setmetatable({{action = "log", message = "raw list"}}, {
            __len = function() error("metamethod should not run") end,
        })
    end
end

function on_stop(ctx)
    return {
        {action = "log", message = "stopping"},
        {action = "send", message_type = "stop.delayed", delay_ms = 10},
    }
end
