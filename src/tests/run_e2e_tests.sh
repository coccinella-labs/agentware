#!/usr/bin/expect -f

set timeout 30
set test_passed 1
set BINARY_PATH $env(BINARY_PATH)

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
