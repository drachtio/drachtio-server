/**
 * Test for SIP header name validation according to RFC 3261
 *
 * RFC 3261 Section 25.1 defines token as:
 *   token = 1*(alphanum / "-" / "." / "!" / "%" / "*" / "_" / "+" / "`" / "'" / "~")
 *
 * Headers like P-Com.Nokia.B2BUA-Involved and P-com.Siemens.IMSI-ID are valid.
 */

#include <iostream>
#include <string>
#include <vector>

using namespace std;

// RFC 3261 token characters
static const char* RFC3261_TOKEN_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890-._!%*+`'~";

bool isValidHeaderName(const string& hdrName) {
    if (hdrName.empty()) return false;
    return string::npos == hdrName.find_first_not_of(RFC3261_TOKEN_CHARS);
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

        // Valid vendor headers with dots (the fix)
        {"P-Com.Nokia.B2BUA-Involved", true, "Nokia vendor header with dots"},
        {"P-com.Siemens.IMSI-ID", true, "Siemens vendor header with dots"},
        {"P-NOKIA.Traffica", true, "Nokia Traffica header with dot"},

        // Valid headers with RFC 3261 token characters
        {"X-Header!", true, "Header with exclamation mark"},
        {"X-Header%Value", true, "Header with percent"},
        {"X-Header*Star", true, "Header with asterisk"},
        {"X-Header+Plus", true, "Header with plus"},
        {"X-Header~Tilde", true, "Header with tilde"},

        // Invalid headers
        {"Invalid Header", false, "Header with space"},
        {"Invalid:Header", false, "Header with colon"},
        {"Invalid@Header", false, "Header with at sign"},
        {"Invalid/Header", false, "Header with forward slash"},
        {"", false, "Empty header name"},
    };

    int passed = 0;
    int failed = 0;

    cout << "Testing SIP header name validation (RFC 3261)" << endl;
    cout << "==============================================" << endl;

    for (const auto& tc : testCases) {
        bool result = isValidHeaderName(tc.headerName);
        bool testPassed = (result == tc.expectedValid);

        if (testPassed) {
            passed++;
            cout << "[PASS] " << tc.description << endl;
        } else {
            failed++;
            cout << "[FAIL] " << tc.description << ": \"" << tc.headerName << "\"" << endl;
        }
    }

    cout << endl << "Results: " << passed << " passed, " << failed << " failed" << endl;

    return (failed > 0) ? 1 : 0;
}
