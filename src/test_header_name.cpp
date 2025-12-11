/**
 * Test for SIP header name validation according to RFC 3261
 *
 * This tests that header names with dots (.) and other RFC 3261 token characters
 * are accepted as valid. Headers like P-Com.Nokia.B2BUA-Involved and
 * P-com.Siemens.IMSI-ID are commonly used in IMS/VoLTE networks.
 *
 * RFC 3261 Section 25.1 defines token as:
 *   token = 1*(alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~")
 *
 * Build: g++ -std=c++17 -o test_header_name test_header_name.cpp -I../deps/boost
 * Run: ./test_header_name
 */

#include <iostream>
#include <string>
#include <vector>
#include <cassert>

using namespace std;

// RFC 3261 token characters: alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~"
static const char* VALID_HEADER_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890-._!%*+`'~";

bool isValidHeaderName(const string& hdrName) {
    return string::npos == hdrName.find_first_not_of(VALID_HEADER_CHARS);
}

struct TestCase {
    string headerName;
    bool expectedValid;
    string description;
};

int main() {
    vector<TestCase> testCases = {
        // Valid standard headers
        {"Content-Type", true, "Standard header with hyphen"},
        {"Via", true, "Simple header name"},
        {"Call-ID", true, "Header with hyphen and uppercase"},
        {"CSeq", true, "Mixed case header"},

        // Valid vendor headers with dots (the issue being fixed)
        {"P-Com.Nokia.B2BUA-Involved", true, "Nokia vendor header with dots"},
        {"P-com.Siemens.IMSI-ID", true, "Siemens vendor header with dots"},
        {"P-NOKIA.Traffica", true, "Nokia Traffica header with dot"},
        {"X-Custom.Header.Name", true, "Custom header with multiple dots"},

        // Valid headers with other RFC 3261 token characters
        {"X-Header!", true, "Header with exclamation mark"},
        {"X-Header%Value", true, "Header with percent"},
        {"X-Header*Star", true, "Header with asterisk"},
        {"X-Header+Plus", true, "Header with plus"},
        {"X-Header`Backtick", true, "Header with backtick"},
        {"X-Header'Quote", true, "Header with single quote"},
        {"X-Header~Tilde", true, "Header with tilde"},
        {"X-Mixed._!%*+`'~", true, "Header with all special chars"},

        // Invalid headers
        {"Invalid Header", false, "Header with space"},
        {"Invalid:Header", false, "Header with colon"},
        {"Invalid@Header", false, "Header with at sign"},
        {"Invalid/Header", false, "Header with forward slash"},
        {"Invalid\\Header", false, "Header with backslash"},
        {"Invalid<Header", false, "Header with less than"},
        {"Invalid>Header", false, "Header with greater than"},
        {"Invalid[Header", false, "Header with bracket"},
        {"Invalid]Header", false, "Header with bracket"},
        {"Invalid{Header", false, "Header with brace"},
        {"Invalid}Header", false, "Header with brace"},
        {"Invalid\"Header", false, "Header with double quote"},
        {"Invalid(Header", false, "Header with parenthesis"},
        {"Invalid)Header", false, "Header with parenthesis"},
        {"Invalid=Header", false, "Header with equals sign"},
        {"", false, "Empty header name"},
    };

    int passed = 0;
    int failed = 0;

    cout << "Testing SIP header name validation (RFC 3261 token compliance)" << endl;
    cout << "==============================================================" << endl << endl;

    for (const auto& tc : testCases) {
        bool result = tc.headerName.empty() ? false : isValidHeaderName(tc.headerName);
        bool testPassed = (result == tc.expectedValid);

        if (testPassed) {
            passed++;
            cout << "[PASS] " << tc.description << ": \"" << tc.headerName << "\"" << endl;
        } else {
            failed++;
            cout << "[FAIL] " << tc.description << ": \"" << tc.headerName << "\"" << endl;
            cout << "       Expected: " << (tc.expectedValid ? "valid" : "invalid")
                 << ", Got: " << (result ? "valid" : "invalid") << endl;
        }
    }

    cout << endl;
    cout << "==============================================================" << endl;
    cout << "Results: " << passed << " passed, " << failed << " failed" << endl;

    if (failed > 0) {
        cout << "TEST SUITE FAILED" << endl;
        return 1;
    }

    cout << "TEST SUITE PASSED" << endl;
    return 0;
}
