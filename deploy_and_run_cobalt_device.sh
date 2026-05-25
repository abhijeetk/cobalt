#!/bin/bash

# Script to deploy and run Cobalt on physical tvOS device or simulator
# Usage: ./deploy_and_run_cobalt_device.sh [OPTIONS] [-- cobalt_args...]
# URL and user-agent are optional; Cobalt's C++ code has its own defaults.

set -e

# Global verbose flag
VERBOSE=false

# Arrays to store device information
declare -a DEVICE_NAMES
declare -a DEVICE_IDS
declare -a DEVICE_TYPES
declare -a DEVICE_STATES

# Function to run command with optional verbose output
run_cmd() {
    if [ "$VERBOSE" = true ]; then
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "[VERBOSE] Executing command (copy-paste ready):"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

        # Build the command string with proper quoting
        local cmd_str=""
        for arg in "$@"; do
            # Quote arguments that contain spaces or special characters
            if [[ "$arg" =~ [[:space:]\|\&\;\<\>\(\)\$\`\\\"\'\*\?\[\]\#\~\=] ]]; then
                # Escape single quotes by replacing ' with '\''
                local escaped_arg="${arg//\'/\'\\\'\'}"
                cmd_str="$cmd_str '$escaped_arg'"
            else
                cmd_str="$cmd_str $arg"
            fi
        done

        # Print the command (trim leading space)
        echo "${cmd_str:1}"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
    fi
    # Execute the command
    "$@"
}

# Function to list all physical devices with their state
list_devices() {
    echo "=========================================="
    echo "Available Physical Devices:"
    echo "=========================================="
    xcrun devicectl list devices 2>&1
    echo ""
}

# Function to collect all devices and simulators
collect_all_devices() {
    local index=0

    # Collect physical devices
    echo "Scanning for physical devices..."
    while IFS= read -r line; do
        # Skip header lines
        if [[ "$line" =~ ^Name.*Identifier.*State ]] || [[ "$line" =~ ^---.*--- ]]; then
            continue
        fi

        # Parse device line (format: Name, Hostname, Identifier, State, Model)
        if [[ "$line" =~ ^(.+[^[:space:]])[[:space:]]+([^[:space:]]+\.coredevice\.local)[[:space:]]+([A-F0-9-]+)[[:space:]]+(available|unavailable|disconnected)[[:space:]]+(.+)$ ]]; then
            local name=$(echo "${BASH_REMATCH[1]}" | xargs)  # trim whitespace
            local uuid="${BASH_REMATCH[3]}"
            local state="${BASH_REMATCH[4]}"
            local model="${BASH_REMATCH[5]}"

            DEVICE_NAMES[$index]="$name"
            DEVICE_IDS[$index]="$uuid"
            DEVICE_TYPES[$index]="Physical - $model"
            DEVICE_STATES[$index]="$state"
            ((index++))
        fi
    done < <(xcrun devicectl list devices 2>&1 | tail -n +2)

    # Collect tvOS simulators
    echo "Scanning for tvOS simulators..."
    while IFS= read -r line; do
        # Match simulator lines like: "Apple TV 4K (3rd generation) (UUID) (State)"
        if [[ "$line" =~ ^[[:space:]]*(.+)[[:space:]]\(([A-F0-9-]+)\)[[:space:]]\((Booted|Shutdown)\) ]]; then
            local name=$(echo "${BASH_REMATCH[1]}" | xargs)
            local uuid="${BASH_REMATCH[2]}"
            local state="${BASH_REMATCH[3]}"

            # Only add tvOS simulators
            DEVICE_NAMES[$index]="$name"
            DEVICE_IDS[$index]="$uuid"
            DEVICE_TYPES[$index]="Simulator - tvOS"
            DEVICE_STATES[$index]="$state"
            ((index++))
        fi
    done < <(xcrun simctl list devices available 2>&1 | grep -A 100 "tvOS" | grep -E "Apple TV")

    echo "Found $index devices/simulators"
    echo ""
}

# Function to display devices and let user choose
select_device_interactively() {
    echo "=========================================="
    echo "Available Devices and Simulators"
    echo "=========================================="
    echo ""

    if [ ${#DEVICE_NAMES[@]} -eq 0 ]; then
        echo "ERROR: No devices or simulators found!"
        echo ""
        echo "TROUBLESHOOTING:"
        echo "1. For physical devices: Connect via USB and trust this computer"
        echo "2. For simulators: Open Xcode and create a tvOS simulator"
        echo "3. Try: open /Applications/Xcode.app"
        exit 1
    fi

    # Display all devices with numbers
    for i in "${!DEVICE_NAMES[@]}"; do
        local num=$((i + 1))
        local status_icon="✓"
        local status_color=""

        if [[ "${DEVICE_STATES[$i]}" == "unavailable" ]] || [[ "${DEVICE_STATES[$i]}" == "disconnected" ]]; then
            status_icon="✗"
        elif [[ "${DEVICE_STATES[$i]}" == "Shutdown" ]]; then
            status_icon="○"
        fi

        printf "%2d) %s %-40s [%s]\n" "$num" "$status_icon" "${DEVICE_NAMES[$i]}" "${DEVICE_TYPES[$i]}"
        printf "     UUID: %s | State: %s\n" "${DEVICE_IDS[$i]}" "${DEVICE_STATES[$i]}"
        echo ""
    done

    echo "=========================================="
    echo "Legend: ✓=Available ✗=Unavailable ○=Shutdown"
    echo ""

    # Ask user to select
    while true; do
        read -p "Select device (1-${#DEVICE_NAMES[@]}) or 'q' to quit: " choice

        if [[ "$choice" == "q" ]] || [[ "$choice" == "Q" ]]; then
            echo "Cancelled by user"
            exit 0
        fi

        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#DEVICE_NAMES[@]}" ]; then
            local idx=$((choice - 1))
            SELECTED_DEVICE_NAME="${DEVICE_NAMES[$idx]}"
            SELECTED_DEVICE_ID="${DEVICE_IDS[$idx]}"
            SELECTED_DEVICE_TYPE="${DEVICE_TYPES[$idx]}"
            SELECTED_DEVICE_STATE="${DEVICE_STATES[$idx]}"

            echo ""
            echo "Selected: ${SELECTED_DEVICE_NAME}"
            echo "UUID: ${SELECTED_DEVICE_ID}"
            echo "Type: ${SELECTED_DEVICE_TYPE}"
            echo "State: ${SELECTED_DEVICE_STATE}"
            echo ""

            # Warn if unavailable
            if [[ "$SELECTED_DEVICE_STATE" == "unavailable" ]] || [[ "$SELECTED_DEVICE_STATE" == "disconnected" ]]; then
                echo "⚠️  WARNING: Device is ${SELECTED_DEVICE_STATE}"
                echo ""
                echo "TROUBLESHOOTING STEPS:"
                echo "1. Unplug and replug your device"
                echo "2. On Apple TV: Settings > Remotes and Devices > Remote App and Devices > Trust"
                echo "3. Open Xcode > Window > Devices and Simulators"
                echo ""
                read -p "Continue anyway? (y/N): " -n 1 -r
                echo
                if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                    exit 1
                fi
            fi

            break
        else
            echo "Invalid choice. Please enter a number between 1 and ${#DEVICE_NAMES[@]}, or 'q' to quit."
        fi
    done
}

# Function to check code signing identity
check_code_signing_identity() {
    echo "Checking code signing identity..."
    local identities=$(security find-identity -v -p codesigning 2>&1)

    if echo "$identities" | grep -q "0 valid identities found"; then
        echo "❌ ERROR: No valid code signing identities found!"
        echo ""
        echo "TROUBLESHOOTING:"
        echo "1. Open Xcode > Settings > Accounts"
        echo "2. Sign in with your Apple ID"
        echo "3. Select your team and click 'Download Manual Profiles'"
        echo "4. Or install provisioning profiles from Apple Developer Portal"
        return 1
    fi

    echo "$identities"
    echo "✓ Code signing identities found"
    return 0
}

# Function to check provisioning profiles
check_provisioning_profiles() {
    local bundle_id="$1"
    echo "Checking provisioning profiles for bundle ID: $bundle_id..."

    local profile_dirs=(
        "$HOME/Library/MobileDevice/Provisioning Profiles"
        "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
    )

    local found_profiles=0
    local all_profiles=()

    for profile_dir in "${profile_dirs[@]}"; do
        if [ -d "$profile_dir" ]; then
            while IFS= read -r -d '' profile; do
                all_profiles+=("$profile")
                ((found_profiles++))
            done < <(find "$profile_dir" -name "*.mobileprovision" -print0 2>/dev/null)

            local profile_count=$(find "$profile_dir" -name "*.mobileprovision" 2>/dev/null | wc -l)
            if [ "$profile_count" -gt 0 ]; then
                echo "  Found $profile_count profiles in $profile_dir"
            fi
        fi
    done

    if [ "$found_profiles" -eq 0 ]; then
        echo "⚠️  WARNING: No provisioning profiles found!"
        echo ""
        echo "TROUBLESHOOTING:"
        echo "1. Open Xcode > Window > Devices and Simulators"
        echo "2. Connect your device and ensure it's paired"
        echo "3. Open Xcode > Settings > Accounts > Download Manual Profiles"
        echo "4. Or manually install .mobileprovision files from Apple Developer Portal"
        echo ""
        read -p "Continue without provisioning profiles? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            return 1
        fi
    else
        echo "✓ Found $found_profiles provisioning profile(s)"
        echo ""
        echo "Analyzing profiles for bundle ID: $bundle_id..."

        # Find matching profiles using Python script similar to codesign.py logic
        find_matching_profile "$bundle_id" "${all_profiles[@]}"

        # Store the selected profile UUID for later comparison
        SELECTED_PROFILE_UUID=""
        if [ -n "$best_profile" ]; then
            local selected_data=$(security cms -D -i "$best_profile" 2>/dev/null)
            SELECTED_PROFILE_UUID=$(echo "$selected_data" | grep -A1 "<key>UUID</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
        fi
    fi

    return 0
}

# Function to find which provisioning profile matches the bundle ID
find_matching_profile() {
    local bundle_id="$1"
    shift
    local profiles=("$@")

    echo ""
    echo "Searching for matching provisioning profile..."

    local matching_profiles=()
    local best_profile=""
    local best_profile_name=""
    local best_profile_team=""
    local best_profile_expiry=""
    local best_profile_app_id=""
    local longest_match=0

    for profile in "${profiles[@]}"; do
        # Extract profile info using security cms
        local profile_data=$(security cms -D -i "$profile" 2>/dev/null)

        if [ -z "$profile_data" ]; then
            continue
        fi

        # Extract key fields using plutil or grep
        local name=$(echo "$profile_data" | grep -A1 "<key>Name</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
        local app_id_pattern=$(echo "$profile_data" | grep -A1 "<key>application-identifier</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
        local team_id=$(echo "$profile_data" | grep -A1 "<key>TeamIdentifier</key>" -m1 | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
        local expiry=$(echo "$profile_data" | grep -A1 "<key>ExpirationDate</key>" | tail -1 | sed 's/.*<date>\(.*\)<\/date>.*/\1/')

        # Check if profile can sign this bundle ID
        # The app ID pattern is like: TEAM_ID.bundle.identifier or TEAM_ID.*
        if [ -n "$app_id_pattern" ]; then
            # Convert wildcard pattern to regex-compatible pattern for matching
            # Remove team prefix for comparison
            local pattern_without_team=$(echo "$app_id_pattern" | sed 's/^[^.]*\.//')

            # Check if bundle_id matches the pattern
            # Handle wildcards: *.app -> matches any.app, com.* -> matches com.anything
            local matches=false

            if [[ "$pattern_without_team" == "$bundle_id" ]]; then
                # Exact match
                matches=true
            elif [[ "$pattern_without_team" == "*" ]]; then
                # Wildcard match all
                matches=true
            elif [[ "$pattern_without_team" == *"*"* ]]; then
                # Pattern contains wildcard
                local pattern_regex=$(echo "$pattern_without_team" | sed 's/\./\\./g' | sed 's/\*/.*/')
                if [[ "$bundle_id" =~ ^${pattern_regex}$ ]]; then
                    matches=true
                fi
            fi

            if [ "$matches" = true ]; then
                matching_profiles+=("$profile")

                # Select the most specific profile (longest pattern)
                local pattern_length=${#pattern_without_team}
                if [ "$pattern_length" -gt "$longest_match" ]; then
                    longest_match=$pattern_length
                    best_profile="$profile"
                    best_profile_name="$name"
                    best_profile_team="$team_id"
                    best_profile_expiry="$expiry"
                    best_profile_app_id="$app_id_pattern"
                fi
            fi
        fi
    done

    local match_count=${#matching_profiles[@]}

    if [ "$match_count" -eq 0 ]; then
        echo "⚠️  WARNING: No provisioning profile matches bundle ID '$bundle_id'"
        echo ""
        echo "EXPLANATION:"
        echo "  Your app requires a provisioning profile with application identifier"
        echo "  pattern that matches '$bundle_id'"
        echo ""
        echo "TROUBLESHOOTING:"
        echo "1. Go to https://developer.apple.com/account/resources/profiles"
        echo "2. Create a provisioning profile for App ID: $bundle_id"
        echo "3. Download and install the .mobileprovision file"
        echo "4. Or use Xcode > Settings > Accounts > Download Manual Profiles"
        return 1
    else
        echo "✓ Found $match_count matching profile(s) for bundle ID '$bundle_id'"
        echo ""

        # Show all matching profiles
        if [ "$match_count" -gt 1 ]; then
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo "ALL MATCHING PROVISIONING PROFILES:"
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

            local idx=1
            for profile in "${matching_profiles[@]}"; do
                # Re-extract profile info for display
                local profile_data=$(security cms -D -i "$profile" 2>/dev/null)
                local p_name=$(echo "$profile_data" | grep -A1 "<key>Name</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
                local p_app_id=$(echo "$profile_data" | grep -A1 "<key>application-identifier</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
                local p_team=$(echo "$profile_data" | grep -A1 "<key>TeamIdentifier</key>" -m1 | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
                local p_expiry=$(echo "$profile_data" | grep -A1 "<key>ExpirationDate</key>" | tail -1 | sed 's/.*<date>\(.*\)<\/date>.*/\1/')
                local p_pattern_without_team=$(echo "$p_app_id" | sed 's/^[^.]*\.//')
                local p_pattern_length=${#p_pattern_without_team}

                echo ""
                echo "[$idx] Profile:"
                echo "    Name:           $p_name"
                echo "    App ID:         $p_app_id"
                echo "    Team:           $p_team"
                echo "    Expiration:     $p_expiry"
                echo "    Pattern Length: $p_pattern_length chars"
                echo "    File:           $(basename "$profile")"

                # Calculate days until expiry
                if [ -n "$p_expiry" ]; then
                    local p_expiry_epoch=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$p_expiry" +%s 2>/dev/null || echo "0")
                    local p_now_epoch=$(date +%s)
                    local p_days_until_expiry=$(( (p_expiry_epoch - p_now_epoch) / 86400 ))
                    if [ "$p_days_until_expiry" -lt 0 ]; then
                        echo "    Status:         ❌ EXPIRED"
                    elif [ "$p_days_until_expiry" -lt 14 ]; then
                        echo "    Status:         ⚠️  Expires in $p_days_until_expiry days"
                    else
                        echo "    Status:         ✓ Valid ($p_days_until_expiry days remaining)"
                    fi
                fi

                # Mark if this is the selected one
                if [ "$profile" = "$best_profile" ]; then
                    echo "    >>> SELECTED: Most specific match (longest pattern)"
                fi

                ((idx++))
            done

            echo ""
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo ""
        fi

        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "SELECTED PROVISIONING PROFILE:"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "  Name:                $best_profile_name"
        echo "  Team ID:             $best_profile_team"
        echo "  App ID Pattern:      $best_profile_app_id"
        echo "  Expiration:          $best_profile_expiry"
        echo "  File:                $(basename "$best_profile")"
        echo ""
        echo "SELECTION CRITERIA:"
        echo "  ✓ Most specific match (longest application identifier pattern)"
        echo "  ✓ Pattern length: $longest_match characters"

        if [ "$match_count" -gt 1 ]; then
            echo ""
            echo "WHY THIS ONE?"
            echo "  When multiple provisioning profiles match, we select the one with"
            echo "  the longest (most specific) App ID pattern. This follows Apple's"
            echo "  recommendation and iOS code signing best practices."
            echo ""
            echo "  Example hierarchy (most to least specific):"
            echo "    1. Exact:    TEAM.abhijeet.tvos.org.chromium.chrome.unittests.dev"
            echo "    2. Wildcard: TEAM.abhijeet.tvos.*"
            echo "    3. Wildcard: TEAM.abhijeet.*"
            echo "    4. Wildcard: TEAM.*"
        fi
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

        # Check expiration warning
        if [ -n "$best_profile_expiry" ]; then
            local expiry_epoch=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$best_profile_expiry" +%s 2>/dev/null || echo "0")
            local now_epoch=$(date +%s)
            local days_until_expiry=$(( (expiry_epoch - now_epoch) / 86400 ))

            if [ "$days_until_expiry" -lt 0 ]; then
                echo ""
                echo "❌ ERROR: Selected provisioning profile has EXPIRED!"
                echo "   Expired: $(date -r $expiry_epoch 2>/dev/null || echo "$best_profile_expiry")"
                return 1
            elif [ "$days_until_expiry" -lt 14 ]; then
                echo ""
                echo "⚠️  WARNING: Profile expires in $days_until_expiry days"
                echo "   Please renew soon at https://developer.apple.com/account"
            fi
        fi

        return 0
    fi
}

# Function to verify app code signature
check_app_codesign() {
    local app_path="$1"
    local bundle_id="$2"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "CODE SIGNATURE VERIFICATION"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "App Path: $app_path"
    echo ""

    # Check if app bundle exists
    if [ ! -d "$app_path" ]; then
        echo "❌ ERROR: App bundle not found!"
        return 1
    fi

    # Check 1: Basic signature verification
    echo "[1/5] Basic Signature Verification..."
    echo "      Running: codesign --verify --verbose=2"
    echo ""

    local verify_output=$(codesign --verify --verbose=2 "$app_path" 2>&1)
    local verify_result=$?

    if [ $verify_result -ne 0 ]; then
        echo "⚠️  Signature verification FAILED"
        echo ""
        echo "Verification output:"
        echo "$verify_output" | sed 's/^/      /'
        echo ""
        echo "This may be expected if the app hasn't been signed yet."
        echo "The deployment process will sign it with proper credentials."
        echo ""
    else
        echo "✓ Basic signature is valid"
        if [ -n "$verify_output" ]; then
            echo "  Details: $verify_output"
        fi
        echo ""
    fi

    # Check 2: Detailed signature information
    echo "[2/5] Detailed Signature Information..."
    echo "      Running: codesign -dvvv --entitlements -"
    echo ""

    local signature_details=$(codesign -dvvv "$app_path" 2>&1)

    # Extract key information
    local identifier=$(echo "$signature_details" | grep "^Identifier=" | cut -d'=' -f2)
    local format=$(echo "$signature_details" | grep "^Format=" | cut -d'=' -f2)
    local authority=$(echo "$signature_details" | grep "^Authority=" | head -1 | cut -d'=' -f2)
    local team_id=$(echo "$signature_details" | grep "^TeamIdentifier=" | cut -d'=' -f2)
    local signing_time=$(echo "$signature_details" | grep "^Signed Time=" | cut -d'=' -f2)
    local info_plist=$(echo "$signature_details" | grep "^Info.plist=" | cut -d'=' -f2)

    if [ -n "$identifier" ]; then
        echo "  Bundle Identifier:  $identifier"
        if [ -n "$bundle_id" ] && [ "$identifier" != "$bundle_id" ]; then
            echo "  ⚠️  MISMATCH: Expected '$bundle_id' but found '$identifier'"
        fi
    else
        echo "  Bundle Identifier:  ⚠️  Not found"
    fi

    if [ -n "$format" ]; then
        echo "  Format:             $format"
    fi

    if [ -n "$authority" ]; then
        echo "  Signing Authority:  $authority"
    else
        echo "  Signing Authority:  ⚠️  Not signed or ad-hoc signature"
    fi

    if [ -n "$team_id" ]; then
        echo "  Team Identifier:    $team_id"
    else
        echo "  Team Identifier:    ⚠️  Not found (ad-hoc or unsigned)"
    fi

    if [ -n "$signing_time" ]; then
        echo "  Signed Time:        $signing_time"
    fi

    echo ""

    # Check 3: Signature chain
    echo "[3/5] Certificate Chain..."
    local all_authorities=$(echo "$signature_details" | grep "^Authority=")

    if [ -n "$all_authorities" ]; then
        echo "✓ Certificate chain found:"
        echo "$all_authorities" | sed 's/^Authority=/      /' | nl -w2 -s'. '
    else
        echo "⚠️  No certificate chain (unsigned or ad-hoc signature)"
    fi
    echo ""

    # Check 4: Embedded provisioning profile
    echo "[4/5] Embedded Provisioning Profile..."
    local embedded_profile="$app_path/embedded.mobileprovision"

    if [ -f "$embedded_profile" ]; then
        echo "✓ Provisioning profile is embedded"

        # Extract profile info
        local profile_info=$(security cms -D -i "$embedded_profile" 2>/dev/null)
        if [ -n "$profile_info" ]; then
            local profile_name=$(echo "$profile_info" | grep -A1 "<key>Name</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
            local profile_uuid=$(echo "$profile_info" | grep -A1 "<key>UUID</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
            local profile_expiry=$(echo "$profile_info" | grep -A1 "<key>ExpirationDate</key>" | tail -1 | sed 's/.*<date>\(.*\)<\/date>.*/\1/')
            local profile_team=$(echo "$profile_info" | grep -A1 "<key>TeamIdentifier</key>" -m1 | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

            echo "  Profile Name:  $profile_name"
            echo "  Profile UUID:  $profile_uuid"
            echo "  Team ID:       $profile_team"
            echo "  Expires:       $profile_expiry"

            # Check if expired
            if [ -n "$profile_expiry" ]; then
                local expiry_epoch=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$profile_expiry" +%s 2>/dev/null || echo "0")
                local now_epoch=$(date +%s)
                if [ "$expiry_epoch" -lt "$now_epoch" ]; then
                    echo "  Status:        ❌ EXPIRED!"
                else
                    local days_left=$(( (expiry_epoch - now_epoch) / 86400 ))
                    echo "  Status:        ✓ Valid ($days_left days remaining)"
                fi
            fi
        fi
    else
        echo "ℹ️  No embedded provisioning profile"
        echo "   This is normal for simulator builds or unsigned apps"
        echo "   Physical device deployment requires a provisioning profile"
    fi
    echo ""

    # Check 5: Code signature verification criteria
    echo "[5/5] Verification Criteria Summary..."
    echo ""
    echo "WHAT WE CHECK:"
    echo "  1. Signature Validity    - Cryptographic signature is intact"
    echo "  2. Bundle Integrity      - All files match signature"
    echo "  3. Certificate Chain     - Valid signing certificate path"
    echo "  4. Team Identifier       - Matches provisioning profile"
    echo "  5. Bundle Identifier     - Matches app configuration"
    echo "  6. Provisioning Profile  - Present and valid for device"
    echo "  7. Entitlements          - Embedded and match profile"
    echo ""

    local criteria_passed=0
    local criteria_total=7

    # Criteria 1: Signature validity
    if [ $verify_result -eq 0 ]; then
        echo "  ✓ [1/7] Signature is cryptographically valid"
        ((criteria_passed++))
    else
        echo "  ✗ [1/7] Signature validation failed"
    fi

    # Criteria 2: Bundle integrity (implied by verify)
    if [ $verify_result -eq 0 ]; then
        echo "  ✓ [2/7] Bundle integrity verified"
        ((criteria_passed++))
    else
        echo "  ✗ [2/7] Bundle integrity check failed"
    fi

    # Criteria 3: Certificate chain
    if [ -n "$authority" ]; then
        echo "  ✓ [3/7] Certificate chain present: $authority"
        ((criteria_passed++))
    else
        echo "  ✗ [3/7] No certificate chain (ad-hoc or unsigned)"
    fi

    # Criteria 4: Team identifier
    if [ -n "$team_id" ]; then
        echo "  ✓ [4/7] Team identifier: $team_id"
        ((criteria_passed++))
    else
        echo "  ✗ [4/7] No team identifier"
    fi

    local bundle_id_mismatch=false
    # Criteria 5: Bundle identifier
    if [ -n "$identifier" ]; then
        if [ -n "$bundle_id" ] && [ "$identifier" != "$bundle_id" ]; then
            echo "  ✗ [5/7] Bundle identifier mismatch. Expected '$bundle_id', but found '$identifier'."
            bundle_id_mismatch=true
        else
            echo "  ✓ [5/7] Bundle identifier: $identifier"
            ((criteria_passed++))
        fi
    else
        echo "  ✗ [5/7] Bundle identifier missing"
    fi

    # Criteria 6: Provisioning profile
    if [ -f "$embedded_profile" ]; then
        echo "  ✓ [6/7] Provisioning profile embedded"
        ((criteria_passed++))
    else
        echo "  ℹ️  [6/7] No provisioning profile (OK for simulator)"
    fi

    # Criteria 7: Entitlements
    local has_entitlements=$(codesign -d --entitlements - --xml "$app_path" 2>/dev/null | grep -c "<!DOCTYPE plist" || true)
    if [ "$has_entitlements" -gt 0 ]; then
        echo "  ✓ [7/7] Entitlements present"
        ((criteria_passed++))
    else
        echo "  ℹ️  [7/7] No entitlements (may be normal for debug)"
    fi

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "VERIFICATION RESULT: $criteria_passed/$criteria_total criteria passed"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    if [ "$bundle_id_mismatch" = true ]; then
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "❌ ERROR: Bundle ID Mismatch Detected!"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        echo "  The application's embedded bundle identifier ('$identifier')"
        echo "  does not match the expected bundle identifier ('$bundle_id') "
        echo "  provided to this script."
        echo ""
        echo "  This usually means the app was built with a different bundle ID"
        echo "  than what is intended for deployment, or the --bundle-id"
        echo "  argument was incorrect."
        echo ""
        echo "TROUBLESHOOTING STEPS:"
        echo "1. Verify the bundle ID in your Xcode project settings (Info.plist)."
        echo "2. Ensure the --bundle-id argument passed to this script is correct."
        echo "3. Clean and rebuild your application to ensure the correct bundle ID is embedded."
        echo "   Example: ninja -C out/tvos-arm64-device_debug cobalt"
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        return 1
    fi

    if [ $verify_result -eq 0 ] && [ "$criteria_passed" -ge 5 ]; then
        echo "✓ Code signature is VALID and ready for deployment"
        return 0
    elif [ "$criteria_passed" -ge 3 ]; then
        echo "⚠️  Code signature is INCOMPLETE but may work for development"
        echo "   The deployment process will re-sign with proper credentials"
        return 0
    else
        echo "⚠️  Code signature verification has issues"
        echo "   The deployment process will sign the app properly"
        return 1
    fi
}

# Function to check entitlements
check_entitlements() {
    local app_path="$1"

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "ENTITLEMENTS VERIFICATION"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    # Extract signature entitlements
    local signature_entitlements=$(codesign -d --entitlements - --xml "$app_path" 2>/dev/null)
    local has_signature_entitlements=false

    if [ -n "$signature_entitlements" ] && echo "$signature_entitlements" | grep -q "<!DOCTYPE plist"; then
        has_signature_entitlements=true
    fi

    # Extract profile entitlements
    local profile_entitlements=""
    local has_profile_entitlements=false
    local embedded_profile="$app_path/embedded.mobileprovision"

    if [ -f "$embedded_profile" ]; then
        local profile_data=$(security cms -D -i "$embedded_profile" 2>/dev/null)
        profile_entitlements=$(echo "$profile_data" | sed -n '/<key>Entitlements<\/key>/,/<\/dict>/p' | tail -n +2)

        if [ -n "$profile_entitlements" ]; then
            has_profile_entitlements=true
        fi
    fi

    # Perform verification if both exist
    if [ "$has_signature_entitlements" = true ] && [ "$has_profile_entitlements" = true ]; then
        echo "✓ Found entitlements in code signature and provisioning profile"
        echo ""

        # Create deploy directory (override previous files)
        local deploy_dir="./deploy"
        rm -rf "$deploy_dir"
        mkdir -p "$deploy_dir"

        # Save signature entitlements
        echo "$signature_entitlements" > "$deploy_dir/signature_entitlements.xml"

        # Save profile entitlements (wrap in plist)
        cat > "$deploy_dir/profile_entitlements.xml" <<'PLISTEOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
PLISTEOF
        echo "$profile_entitlements" >> "$deploy_dir/profile_entitlements.xml"
        echo "</plist>" >> "$deploy_dir/profile_entitlements.xml"

        echo "Saved to: $deploy_dir/signature_entitlements.xml"
        echo "Saved to: $deploy_dir/profile_entitlements.xml"
        echo ""

        # Extract and compare keys
        local sig_keys=$(grep -o '<key>[^<]*</key>' "$deploy_dir/signature_entitlements.xml" | sed 's/<key>\(.*\)<\/key>/\1/' | sort)
        local sig_count=$(echo "$sig_keys" | wc -l | tr -d ' ')

        local prof_keys=$(grep -o '<key>[^<]*</key>' "$deploy_dir/profile_entitlements.xml" | sed 's/<key>\(.*\)<\/key>/\1/' | sort)
        local prof_count=$(echo "$prof_keys" | wc -l | tr -d ' ')

        echo "Checking: Signature keys ($sig_count) must be subset of Profile keys ($prof_count)"
        echo ""

        local missing_keys=0
        local matching_keys=0

        while IFS= read -r sig_key; do
            if [ -n "$sig_key" ]; then
                if echo "$prof_keys" | grep -qx "$sig_key"; then
                    echo "  ✓ $sig_key"
                    ((matching_keys++))
                else
                    echo "  ✗ $sig_key (MISSING)"
                    ((missing_keys++))
                fi
            fi
        done <<< "$sig_keys"

        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "SUMMARY"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "Signature Keys:  $sig_count"
        echo "Profile Keys:    $prof_count"
        echo "Matching:        $matching_keys"
        echo "Missing:         $missing_keys"
        echo ""

        if [ $missing_keys -eq 0 ] && [ $sig_count -gt 0 ]; then
            echo "✅ PASS: All entitlement keys match"
            echo ""
            echo "Your app is properly signed for device deployment."
            echo "Files saved to $deploy_dir/ for manual verification."
        elif [ $missing_keys -gt 0 ]; then
            echo "❌ FAIL: $missing_keys key(s) missing in profile"
            echo ""
            echo "Action required:"
            echo "  1. Check files in $deploy_dir/"
            echo "  2. Rebuild: ninja -C out/tvos-arm64-device_debug cobalt"
        else
            echo "⚠️  WARNING: No keys to verify"
        fi

    elif [ "$has_signature_entitlements" = false ] && [ "$has_profile_entitlements" = false ]; then
        echo "ℹ️  No entitlements found (OK for simulator builds)"
    else
        echo "⚠️  Entitlements mismatch:"
        if [ "$has_signature_entitlements" = false ]; then
            echo "  • Missing in code signature"
        fi
        if [ "$has_profile_entitlements" = false ]; then
            echo "  • Missing in provisioning profile"
        fi
    fi

    echo ""
}

# Function to run all device checks
run_device_checks() {
    local app_path="$1"
    local bundle_id="$2"

    echo ""
    echo "=========================================="
    echo "Pre-deployment Device Checks"
    echo "=========================================="
    echo ""

    # Check 1: Code signing identity (Fatal)
    if ! check_code_signing_identity; then
        echo "❌ ERROR: Code signing identity check failed. Aborting."
        exit 1
    fi
    echo ""

    # Check 2: Provisioning profiles (Fatal)
    if ! check_provisioning_profiles "$bundle_id"; then
        echo "❌ ERROR: Provisioning profile check failed. Aborting."
        exit 1
    fi
    echo ""

    # Check 3: App code signature (Fatal)
    if ! check_app_codesign "$app_path" "$bundle_id"; then
        # Detailed error is printed inside the function
        echo "❌ ERROR: App code signature check failed. Aborting."
        exit 1
    fi
    echo ""

    # Check 4: Entitlements (Informational)
    check_entitlements "$app_path"
    echo ""

    echo "=========================================="
    echo "✓ All pre-deployment checks passed"
    echo "=========================================="


    echo ""
}

# Function to check if a device is available (for non-interactive mode)
check_device_available() {
    local device="$1"
    local device_info=$(xcrun devicectl list devices 2>&1 | grep -i "$device" || true)

    if [ -z "$device_info" ]; then
        echo "ERROR: Device '$device' not found!"
        echo ""
        list_devices
        echo "TROUBLESHOOTING:"
        echo "1. Make sure your device is physically connected via USB"
        echo "2. Unlock your device and trust this computer"
        echo "3. Check Xcode > Window > Devices and Simulators"
        echo "4. Try unplugging and replugging the device"
        return 1
    fi

    if echo "$device_info" | grep -q "unavailable"; then
        echo "WARNING: Device '$device' is in 'unavailable' state"
        echo "Device info: $device_info"
        echo ""
        echo "TROUBLESHOOTING STEPS:"
        echo "1. Unplug and replug your Apple TV/device"
        echo "2. On the device, go to Settings > Remotes and Devices > Remote App and Devices"
        echo "3. Trust this computer if prompted"
        echo "4. Open Xcode > Window > Devices and Simulators to trigger pairing"
        echo "5. Try running: open /Applications/Xcode.app"
        echo ""
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    elif echo "$device_info" | grep -q "available"; then
        echo "✓ Device is available: $device_info"
    fi
}

# Function to show help
show_help() {
    cat <<'EOF'
NAME
    deploy_and_run_cobalt_device.sh - Deploy and run Cobalt on tvOS devices

SYNOPSIS
    deploy_and_run_cobalt_device.sh [OPTIONS] [-- cobalt_args...]

DESCRIPTION
    Deploy and launch Cobalt application on tvOS physical devices or simulators.
    Supports interactive device selection, automatic code signing verification,
    and provisioning profile validation.

    All options are optional. Without any options, uses interactive mode with
    default bundle ID (foo.cobalt.cobalt). URL and user-agent are not passed
    unless explicitly specified; Cobalt's C++ code provides its own defaults.

OPTIONS
    --url URL
        URL to launch in Cobalt application
        Optional: If not provided, Cobalt uses its own built-in default URL

    --bundle-id ID
        Application bundle identifier for signing and deployment
        Default: foo.cobalt.cobalt

    --build-type TYPE
        Build type to deploy: qa, debug, or gold
        Default: debug

    --device UUID
        Device UUID or name for direct deployment (skips interactive selection)
        If omitted, enters interactive mode to select from available devices

    --list, -l
        List all available physical devices and simulators, then exit

    --verbose, -v
        Enable verbose mode to print all device communication commands
        before executing them. Useful for debugging deployment issues.

    --help, -h
        Display this help message and exit

MODES
    Interactive Mode (Default)
        When --device is not specified, prompts user to select from available
        devices and simulators. Displays device name, UUID, type, and state.

    Direct Mode
        When --device is specified, deploys directly to that device without
        prompting. Device can be specified by UUID or name.

EXAMPLES
    Run with all defaults (interactive device selection, Cobalt's built-in URL):
        ./deploy_and_run_cobalt_device.sh

    Specify custom URL (overrides Cobalt's built-in default):
        ./deploy_and_run_cobalt_device.sh --url 'https://crosvideo.appspot.com'

    Custom bundle ID and URL:
        ./deploy_and_run_cobalt_device.sh \
            --bundle-id 'com.example.app' \
            --url 'https://example.com'

    Deploy QA build:
        ./deploy_and_run_cobalt_device.sh --build-type qa

    Deploy gold build to specific device:
        ./deploy_and_run_cobalt_device.sh \
            --build-type gold \
            --device 'Abhijeet-TVOS'

    Deploy to specific device:
        ./deploy_and_run_cobalt_device.sh \
            --device 'Abhijeet-TVOS' \
            --url 'https://www.youtube.com/tv'

    Pass additional arguments to Cobalt:
        ./deploy_and_run_cobalt_device.sh \
            --url 'https://example.com' \
            -- --enable-logging=stderr --v=3

    Enable verbose mode to see all device commands:
        ./deploy_and_run_cobalt_device.sh --verbose

    Combine verbose with other options:
        ./deploy_and_run_cobalt_device.sh \
            --verbose \
            --device 'Abhijeet-TVOS' \
            --url 'https://www.youtube.com/tv'

    List all available devices:
        ./deploy_and_run_cobalt_device.sh --list

FILES
    ./deploy/
        Directory containing deployment verification files:
        - signature_entitlements.xml: Code signature entitlements
        - profile_entitlements.xml: Provisioning profile entitlements

ENVIRONMENT
    No environment variables required. Uses system tools:
    xcrun, devicectl, simctl, codesign, security

EXIT STATUS
    0    Successful deployment and launch
    1    Error during device detection, verification, or deployment

NOTES
    • Physical devices require valid code signing identity and provisioning profile
    • Simulators do not require code signing
    • Pre-deployment checks are informational only and do not block deployment
    • Use './deploy/' directory to manually verify entitlements if needed
    • URL and user-agent are only passed when explicitly specified via --url flag
    • Cobalt's C++ code (cobalt_switch_defaults.cc) provides built-in defaults
    • Remote debugging is enabled by default on port 9222 (address 0.0.0.0)
    • Access Chrome DevTools at: chrome://inspect or http://<device-ip>:9222
    • Headless mode is enabled by default for automated testing

SEE ALSO
    xcrun(1), devicectl(1), simctl(1), codesign(1), security(1)

EOF
}

# Parse arguments
INTERACTIVE_MODE=false
DEVICE=""
URL=""
BUNDLE_ID=""
BUILD_TYPE=""
BUNDLE_ID_PROVIDED=false
URL_PROVIDED=false
BUILD_TYPE_PROVIDED=false

# Special commands that don't require other args
if [ "$1" = "--list" ] || [ "$1" = "-l" ]; then
    collect_all_devices
    select_device_interactively
    exit 0
fi

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

# Parse options first
while [[ $# -gt 0 ]]; do
    case $1 in
        --url)
            if [ -z "$2" ] || [[ "$2" =~ ^- ]]; then
                echo "ERROR: --url requires a value"
                exit 1
            fi
            URL="$2"
            URL_PROVIDED=true
            shift 2
            ;;
        --bundle-id)
            if [ -z "$2" ] || [[ "$2" =~ ^- ]]; then
                echo "ERROR: --bundle-id requires a value"
                exit 1
            fi
            BUNDLE_ID="$2"
            BUNDLE_ID_PROVIDED=true
            shift 2
            ;;
        --build-type)
            if [ -z "$2" ] || [[ "$2" =~ ^- ]]; then
                echo "ERROR: --build-type requires a value"
                exit 1
            fi
            if [[ ! "$2" =~ ^(qa|debug|gold)$ ]]; then
                echo "ERROR: --build-type must be one of: qa, debug, gold"
                exit 1
            fi
            BUILD_TYPE="$2"
            BUILD_TYPE_PROVIDED=true
            shift 2
            ;;
        --device)
            if [ -z "$2" ] || [[ "$2" =~ ^- ]]; then
                echo "ERROR: --device requires a value"
                exit 1
            fi
            DEVICE="$2"
            shift 2
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --)
            # Everything after -- is passed to Cobalt
            shift
            EXTRA_ARGS=("$@")
            break
            ;;
        -*)
            echo "ERROR: Unknown option: $1"
            echo "Try '$0 --help' for more information."
            exit 1
            ;;
        *)
            # No positional arguments allowed - must use flags
            echo "ERROR: Unexpected argument: $1"
            echo "Use --url, --bundle-id, or --device flags instead."
            echo "Try '$0 --help' for more information."
            exit 1
            ;;
    esac
done

# Set defaults for optional parameters
if [ -z "$BUNDLE_ID" ]; then
    BUNDLE_ID="foo.cobalt.cobalt"
fi

if [ -z "$BUILD_TYPE" ]; then
    BUILD_TYPE="debug"
fi

# Determine if interactive mode (no device specified)
if [ -z "$DEVICE" ]; then
    INTERACTIVE_MODE=true
fi

# If interactive mode, collect devices and let user choose
if [ "$INTERACTIVE_MODE" = true ]; then
    collect_all_devices
    select_device_interactively
    DEVICE="$SELECTED_DEVICE_ID"
    IS_SIMULATOR=false
    if [[ "$SELECTED_DEVICE_TYPE" =~ "Simulator" ]]; then
        IS_SIMULATOR=true
    fi
else
    # Non-interactive: check if device is a simulator UUID
    IS_SIMULATOR=false
    if xcrun simctl list devices 2>&1 | grep -q "$DEVICE"; then
        IS_SIMULATOR=true
    fi
fi

#FAIRPLAY_UA='Mozilla/5.0 (X11; Linux x86_64) Cobalt/26.lts.0-qa (unlike Gecko) v8/unknown gles Starboard/17, SystemIntegratorName_DESKTOP_ChipsetModelNumber_2025/FirmwareVersion (BrandName, ModelName)'
#FAIRPLAY_UA='Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.2 Safari/605.1.15'

# tvOS Safari UA — makes Shaka detect Apple TV and request com.apple.fps.1_0 (FairPlay)
#FAIRPLAY_UA='Mozilla/5.0 (Apple TV; CPU AppleTV14,1 OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/604.1'

# Safari UA triggers YouTube to serve HLS manifests
#FAIRPLAY_UA='Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15'

# Determine app path based on device type and build type
if [ "$IS_SIMULATOR" = true ]; then
    APP_PATH="out/tvos-arm64-simulator_${BUILD_TYPE}/cobalt.app"
    DEVICE_TYPE_NAME="Simulator"
else
    APP_PATH="out/tvos-arm64-device_${BUILD_TYPE}/cobalt.app"
    DEVICE_TYPE_NAME="Physical Device"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "SCRIPT CONFIGURATION"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Device:           $DEVICE"
echo "  Device Type:      $DEVICE_TYPE_NAME"
echo "  App Path:         $APP_PATH"
if [ "$BUILD_TYPE_PROVIDED" = false ]; then
    echo "  Build Type:       $BUILD_TYPE (script default)"
else
    echo "  Build Type:       $BUILD_TYPE"
fi
if [ "$BUNDLE_ID_PROVIDED" = false ]; then
    echo "  Bundle ID:        $BUNDLE_ID (script default)"
else
    echo "  Bundle ID:        $BUNDLE_ID"
fi
echo ""
echo "  ── Args passed to Cobalt by script ──"
if [ "$URL_PROVIDED" = true ]; then
    echo "  URL:              $URL"
else
    echo "  URL:              (not passed, Cobalt will use its C++ default)"
fi
if [ -n "$FAIRPLAY_UA" ]; then
    echo "  User-Agent:       $FAIRPLAY_UA"
else
    echo "  User-Agent:       (not passed, Cobalt will use its C++ default)"
fi
echo "  Remote Debug Addr: 0.0.0.0"
echo "  Headless:          yes"
if [ ${#EXTRA_ARGS[@]} -gt 0 ]; then
    echo "  Extra Args:       ${EXTRA_ARGS[*]}"
fi
echo ""
echo "  ── Script options ──"
echo "  Verbose:           $VERBOSE"
echo "  Interactive:       $INTERACTIVE_MODE"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Check device availability for physical devices (skip for simulators)
if [ "$IS_SIMULATOR" = false ] && [ "$INTERACTIVE_MODE" = false ]; then
    check_device_available "$DEVICE"
    echo ""
fi

# Step 1: Check if app exists
if [ ! -d "$APP_PATH" ]; then
    echo "ERROR: App not found at $APP_PATH"
    if [ "$IS_SIMULATOR" = true ]; then
        echo "Please build first: ninja -C out/tvos-arm64-simulator_${BUILD_TYPE} cobalt -j50"
    else
        echo "Please build first: ninja -C out/tvos-arm64-device_${BUILD_TYPE} cobalt -j50"
    fi
    exit 1
fi

# Step 2: Run pre-deployment checks for physical devices
if [ "$IS_SIMULATOR" = false ]; then
    run_device_checks "$APP_PATH" "$BUNDLE_ID"
fi

if [ "$IS_SIMULATOR" = true ]; then
    # Simulator deployment path
    echo "[1/2] Launching simulator if not running..."
    run_cmd xcrun simctl boot "$DEVICE" 2>/dev/null || echo "Simulator already booted"
    echo ""

    echo "[2/2] Launching Cobalt on simulator..."
    echo "=========================================="
    echo "Remote debugging enabled on port 9222"
    echo "Running in headless mode"
    echo ""

    # Use iossim for simulator
    SIM_LOG_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    SIM_LOG_FILE="/tmp/cobalt_simulator_${BUILD_TYPE}_${SIM_LOG_TIMESTAMP}.log"
    echo -e "\033[1;33mLog file: ${SIM_LOG_FILE}\033[0m"
    echo -e "\033[1;33mTail logs: tail -f ${SIM_LOG_FILE}\033[0m"
    echo ""
    # Build iossim arguments - only pass URL and user-agent if explicitly provided.
    # Cobalt's C++ code (cobalt_switch_defaults.cc) provides its own defaults.
    SIM_LAUNCH_ARGS=()
    SIM_LAUNCH_ARGS+=(-x tvos)
    SIM_LAUNCH_ARGS+=(-d "$DEVICE")
    SIM_LAUNCH_ARGS+=(-v)
    if [ "$URL_PROVIDED" = true ]; then
        SIM_LAUNCH_ARGS+=(-c "$URL")
    fi
    if [ -n "$FAIRPLAY_UA" ]; then
        SIM_LAUNCH_ARGS+=(-c "--user-agent=$FAIRPLAY_UA")
    fi
    SIM_LAUNCH_ARGS+=(-c "--remote-debugging-port=9222")
    SIM_LAUNCH_ARGS+=(-c "--remote-debugging-address=0.0.0.0")
    SIM_LAUNCH_ARGS+=(-c "--headless")
    SIM_LAUNCH_ARGS+=("${EXTRA_ARGS[@]}")
    SIM_LAUNCH_ARGS+=(-i "$APP_PATH")

    run_cmd ./out/tvos-arm64-simulator_debug/iossim \
        "${SIM_LAUNCH_ARGS[@]}" \
        2>&1 | tee "$SIM_LOG_FILE"
else
    # Physical device deployment path
    echo "[1/4] Uninstalling existing Cobalt app (Bundle ID: $BUNDLE_ID)..."
    set +e
    uninstall_output=$(run_cmd xcrun devicectl device uninstall app --device "$DEVICE" "$BUNDLE_ID" 2>&1)
    uninstall_result=$?
    set -e

    if [ $uninstall_result -eq 0 ]; then
        echo "✓ Successfully uninstalled existing app (Bundle ID: $BUNDLE_ID)"
    else
        if echo "$uninstall_output" | grep -q "not installed"; then
            echo "ℹ️  App was not previously installed (this is OK)"
        else
            echo "⚠️  Uninstall failed or app not found (continuing anyway)"
            echo "$uninstall_output" | head -5 | sed 's/^/   /'
        fi
    fi
    echo ""

    # Additional pre-install verification
    echo "[2/4] Pre-install Verification..."
    echo "Verifying app is ready for device installation..."

    # Check code signature is valid
    if codesign --verify --deep --strict "$APP_PATH" 2>&1; then
        echo "✓ Code signature verification passed"
    else
        echo "⚠️  Code signature verification failed"
        echo ""
        echo "Attempting to re-sign the app..."

        # Try to find a valid signing identity
        signing_identity=$(security find-identity -v -p codesigning 2>&1 | grep "Apple Development" | head -1 | grep -o '"[^"]*"' | tr -d '"')

        if [ -n "$signing_identity" ]; then
            echo "Using identity: $signing_identity"
            codesign --force --sign "$signing_identity" --deep "$APP_PATH" 2>&1 || {
                echo "❌ Failed to re-sign app"
            }
        else
            echo "❌ No valid signing identity found"
        fi
    fi
    echo ""

    echo "[3/4] Installing Cobalt app..."
    echo "Installing: $APP_PATH"
    echo "To device: $DEVICE"
    echo "Bundle ID: $BUNDLE_ID"
    echo ""

    install_output=$(run_cmd xcrun devicectl device install app --device "$DEVICE" "$APP_PATH" 2>&1)
    install_result=$?

    if [ $install_result -eq 0 ]; then
        echo "✓ Installation successful for Bundle ID: $BUNDLE_ID"
    else
        echo "❌ Installation FAILED!"
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "INSTALLATION ERROR DETAILS"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "$install_output"
        echo ""

        # Parse specific errors
        if echo "$install_output" | grep -q "error 3002"; then
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo "ERROR TYPE: CoreDeviceError 3002"
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo ""
            echo "This error typically indicates:"
            echo "  1. Code signing issue"
            echo "  2. Provisioning profile mismatch"
            echo "  3. Entitlements incompatibility"
            echo "  4. Device UDID not in provisioning profile"
            echo ""
        fi

        if echo "$install_output" | grep -q "IXErrorDomain error 24"; then
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo "ERROR TYPE: IXErrorDomain 24 (Uninstall Error)"
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo ""
            echo "This error occurs when:"
            echo "  • An app with same bundle ID exists with different signature"
            echo "  • Previous installation is corrupted"
            echo "  • Device has conflicting app data"
            echo ""
        fi

        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "TROUBLESHOOTING STEPS"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        echo "STEP 1: Manually delete the app from your Apple TV"
        echo "  • On Apple TV: Settings > General > Manage Storage"
        echo "  • Find and delete: 'cobalt' or bundle ID starting with 'abhijeet.tvos'"
        echo "  • Or: Long-press the app icon > Delete"
        echo ""
        echo "STEP 2: Verify code signing"
        echo "  Run: codesign -dvv $APP_PATH"
        echo "  Check that Team ID matches your provisioning profile"
        echo ""
        echo "STEP 3: Verify device UDID is in provisioning profile"
        echo "  • Get device UDID: xcrun devicectl list devices"
        echo "  • Check profile: security cms -D -i ~/Library/Developer/Xcode/UserData/Provisioning\\ Profiles/*.mobileprovision | grep -A 1 ProvisionedDevices"
        echo "  • Your device UDID must be in the ProvisionedDevices array"
        echo ""
        echo "STEP 4: Check provisioning profile validity"
        echo "  • Open Xcode > Window > Devices and Simulators"
        echo "  • Select your Apple TV device"
        echo "  • Check if device shows 'Ready' status"
        echo "  • Try: Xcode > Settings > Accounts > Download Manual Profiles"
        echo ""
        echo "STEP 5: Rebuild with clean code signing"
        echo "  rm -rf $APP_PATH"
        echo "  ninja -C out/tvos-arm64-device_debug cobalt"
        echo ""
        echo "STEP 6: Check device free space"
        echo "  • Settings > General > Manage Storage"
        echo "  • Ensure at least 500MB free space"
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "QUICK FIX COMMANDS"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        echo "# Check device UDID"
        echo "xcrun devicectl list devices | grep -A2 'Abhijeet-TVOS'"
        echo ""
        echo "# Check app signature"
        echo "codesign -dvvv $APP_PATH 2>&1 | grep -E 'Identifier|Authority|TeamIdentifier'"
        echo ""
        echo "# Check provisioning profile"
        echo "security cms -D -i $APP_PATH/embedded.mobileprovision 2>/dev/null | grep -A1 'UUID\\|ExpirationDate\\|ProvisionedDevices'"
        echo ""
        echo "# Force uninstall via devicectl (if app still showing)"
        echo "xcrun devicectl device uninstall app --device '$DEVICE' '$BUNDLE_ID'"
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

        # Try to get more diagnostic info
        echo ""
        echo "DIAGNOSTIC INFORMATION:"
        echo ""

        echo "1. Current app signature:"
        codesign -dvvv "$APP_PATH" 2>&1 | grep -E "Identifier=|Authority=|TeamIdentifier=" | sed 's/^/   /'
        echo ""

        echo "2. Embedded provisioning profile:"
        if [ -f "$APP_PATH/embedded.mobileprovision" ]; then
            profile_info=$(security cms -D -i "$APP_PATH/embedded.mobileprovision" 2>/dev/null)
            profile_uuid=$(echo "$profile_info" | grep -A1 "<key>UUID</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
            profile_name=$(echo "$profile_info" | grep -A1 "<key>Name</key>" | tail -1 | sed 's/.*<string>\(.*\)<\/string>.*/\1/')
            device_count=$(echo "$profile_info" | grep -A1 "<key>ProvisionedDevices</key>" -A 100 | grep "<string>" | wc -l)

            echo "   Name: $profile_name"
            echo "   UUID: $profile_uuid"
            echo "   Devices in profile: $device_count"
            echo ""

            # Check if current device UDID is in profile
            current_device_udid=$(xcrun devicectl list devices 2>&1 | grep "$DEVICE" | awk '{print $3}')
            if echo "$profile_info" | grep -q "$current_device_udid"; then
                echo "   ✓ Current device IS in provisioning profile"
            else
                echo "   ❌ Current device NOT in provisioning profile!"
                echo "   Device UDID: $current_device_udid"
                echo ""
                echo "   FIX: Add device to provisioning profile at:"
                echo "        https://developer.apple.com/account/resources/profiles"
            fi
        else
            echo "   ❌ No embedded.mobileprovision found!"
        fi
        echo ""

        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        exit 1
    fi
    echo ""

    if [ "$URL_PROVIDED" = true ]; then
        echo "[4/4] Launching Cobalt (Bundle ID: $BUNDLE_ID) with URL: $URL"
    else
        echo "[4/4] Launching Cobalt (Bundle ID: $BUNDLE_ID) with built-in default URL"
    fi
    echo "=========================================="
    echo "Remote debugging enabled on port 9222"
    echo "Running in headless mode"
    echo ""

    # Build launch arguments - only pass URL and user-agent if explicitly provided.
    # Cobalt's C++ code (cobalt_switch_defaults.cc) provides its own defaults.
    DEV_LOG_TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    DEV_LOG_FILE="/tmp/cobalt_device_${BUILD_TYPE}_${DEV_LOG_TIMESTAMP}.log"
    echo -e "\033[1;33mLog file: ${DEV_LOG_FILE}\033[0m"
    echo -e "\033[1;33mTail logs: tail -f ${DEV_LOG_FILE}\033[0m"
    echo ""

    LAUNCH_ARGS=()
    if [ "$URL_PROVIDED" = true ]; then
        LAUNCH_ARGS+=("$URL")
    fi
    if [ -n "$FAIRPLAY_UA" ]; then
        LAUNCH_ARGS+=("--user-agent=$FAIRPLAY_UA")
    fi
    LAUNCH_ARGS+=("--remote-debugging-port=9222")
    LAUNCH_ARGS+=("--remote-debugging-address=0.0.0.0")
    LAUNCH_ARGS+=("--headless")
    LAUNCH_ARGS+=("${EXTRA_ARGS[@]}")

    run_cmd xcrun devicectl device process launch \
        --device "$DEVICE" \
        --console \
        "$BUNDLE_ID" \
        "${LAUNCH_ARGS[@]}" \
        2>&1 | tee "$DEV_LOG_FILE"
fi
