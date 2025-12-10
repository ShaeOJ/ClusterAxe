const fs = require('fs');
const path = require('path');

let version;
try {
    // Use --abbrev=0 to get just the tag name without commit count/hash suffix
    version = require('child_process').execSync('git describe --tags --abbrev=0').toString().trim();
} catch (e) {
    // Fallback if not in a git repository
    version = 'ESP-Miner-Dual-V5';
}

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
