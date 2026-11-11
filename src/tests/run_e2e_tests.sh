#!/usr/bin/expect -f

set timeout 30
set test_passed 1
set BINARY_PATH $env(BINARY_PATH)

# Directory holding this script. The listing test uses it so the case works
# from the repository root (make test-e2e) and from src/tests (the Docker
# harness), where the relative layouts differ.
set script_dir [file dirname [file normalize $argv0]]

# Function to run a test
proc run_test {test_name mode_selection expected_response} {
    global test_passed BINARY_PATH

    send_user "Running test: $test_name\n"

    # Start the agent
    spawn $BINARY_PATH
    expect "Mode"

    # Select online mode
    send "1\r"
    expect "Provider"

    # Select the provider
    send "$mode_selection\r"
    expect "Ready"

    # Send a test message
    send "Hello\r"
    expect {
        "$expected_response" {
            send_user "✓ Test $test_name passed\n"
        }
        timeout {
            send_user "✗ Test $test_name failed: timeout\n"
            set test_passed 0
        }
        eof {
            send_user "✗ Test $test_name failed: unexpected EOF\n"
            set test_passed 0
        }
    }

    # Exit
    send "exit\r"
    expect eof
}

# Record a failed local case and ask the agent to exit.
proc fail_local_test {test_name stage} {
    global test_passed

    send_user "✗ Test $test_name failed while waiting for $stage\n"
    set test_passed 0
    # Ask the agent to exit but do not wait for EOF here: reaping the spawn
    # inside a nested procedure on macOS closes the parent's stdout.
    catch {send "exit\r"}
}

# Drive the binary into offline mode and run a sequence of command/expected
# pairs. Offline mode needs no API key and no network until a chat is sent, so
# these cases exercise the local command surface only. An empty command means
# the step only waits for more buffered output from the previous command.
proc run_local_test {test_name steps} {
    global test_passed BINARY_PATH

    send_user "Running test: $test_name\n"

    spawn $BINARY_PATH

    set ok 0
    expect {
        "Mode" { set ok 1 }
        timeout { }
        eof { }
    }
    if {!$ok} {
        fail_local_test $test_name "the mode prompt"
        return
    }

    send "2\r"
    set ok 0
    expect {
        "Model" { set ok 1 }
        timeout { }
        eof { }
    }
    if {!$ok} {
        fail_local_test $test_name "the model prompt"
        return
    }

    send "1\r"
    set ok 0
    expect {
        "Ready" { set ok 1 }
        timeout { }
        eof { }
    }
    if {!$ok} {
        fail_local_test $test_name "the prompt"
        return
    }

    foreach {command expected} $steps {
        if {$command ne ""} {
            send "$command\r"
        }
        set ok 0
        expect {
            "$expected" { set ok 1 }
            timeout { }
            eof { }
        }
        if {!$ok} {
            fail_local_test $test_name "'$expected'"
            return
        }
    }

    send "exit\r"
    catch {expect eof}
    send_user "✓ Test $test_name passed\n"
}

# Local command surface. No provider or network is involved.
set conversation_tag "e2e_session"
file delete -force [file join [pwd] data conversations "$conversation_tag.txt"]

# Directory listing
run_local_test "Directory Listing" [list "list:$script_dir" "run_e2e_tests.sh"]

# Tool registry
run_local_test "Tool Registry" [list "/tools" "Available tools:" "" "read:file"]

# Session management. The first step records one interaction so the session
# memory file exists; save copies that file, and a clean checkout has none.
run_local_test "Session Management" [list \
    "list:$script_dir" "run_e2e_tests.sh" \
    "/chat save $conversation_tag" "Conversation saved as: $conversation_tag" \
    "/chat list" "Saved conversations:" "" "$conversation_tag"]

# Configuration check
run_local_test "Configuration Check" [list "version" "Agentware v*"]

file delete -force [file join [pwd] data conversations "$conversation_tag.txt"]

# Test Fireworks
run_test "Fireworks" "3" "Mock Fireworks response"

# Test Groq
run_test "Groq" "4" "Mock Groq response"

# Test DeepSeek
run_test "DeepSeek" "5" "Mock DeepSeek response"

# Test OpenAI
run_test "OpenAI" "6" "Mock OpenAI response"

# Test Together (existing)
run_test "Together" "1" "Mock Together AI response"

# Test Cerebras (existing)
run_test "Cerebras" "2" "Mock Cerebras response"

if {$test_passed} {
    send_user "All E2E tests passed!\n"
    exit 0
} else {
    send_user "Some E2E tests failed!\n"
    exit 1
}
