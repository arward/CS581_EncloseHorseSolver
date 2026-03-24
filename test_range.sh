#!/usr/bin/env bash

BINARY="./bin/enclosed"
RESULTS_FILE="results.csv"
START_DATE="2026-02-01"
END_DATE=$(date +%Y-%m-%d)

# Convert dates to epoch seconds for portable comparison
end_epoch=$(date -d "$END_DATE" +%s)

echo "date,score,optimal,walls_used,budget" > "$RESULTS_FILE"

current="$START_DATE"
total=0
optimal_count=0
failed=0

while [ "$(date -d "$current" +%s)" -le "$end_epoch" ]; do
    output=$("$BINARY" "$current" 2>&1)

    score=$(echo "$output" | grep -oP 'Score: \K[0-9]+')
    optimal=$(echo "$output" | grep -oP 'Optimal score: \K[0-9]+')
    walls_used=$(echo "$output" | grep -oP 'Walls placed: \K[0-9]+')
    budget=$(echo "$output" | grep -oP 'Budget: \K[0-9]+')
    is_optimal=$(echo "$output" | grep -c 'OPTIMAL')

    if [[ -z "$score" ]]; then
        echo "$current  FAILED"
        echo "$current,,,," >> "$RESULTS_FILE"
        failed=$((failed + 1))
    else
        total=$((total + 1))
        if [[ "$is_optimal" -gt 0 ]]; then
            optimal_count=$((optimal_count + 1))
            tag="OPTIMAL"
        else
            tag="$score/$optimal"
        fi
        echo "$current  $tag"
        echo "$current,$score,$optimal,$walls_used,$budget" >> "$RESULTS_FILE"
    fi

    current=$(date -d "$current + 1 day" +%Y-%m-%d)
done

echo ""
echo "=== Summary ==="
echo "Total puzzles: $total"
echo "Optimal: $optimal_count / $total"
echo "Failed: $failed"
echo "Results saved to $RESULTS_FILE"
