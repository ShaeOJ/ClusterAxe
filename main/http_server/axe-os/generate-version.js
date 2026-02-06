const fs = require('fs');
const path = require('path');

let version;
try {
    // Read version from project root version.txt (single source of truth)
    const versionFile = path.join(__dirname, '..', '..', '..', 'version.txt');
    version = fs.readFileSync(versionFile, 'utf8').trim();
} catch (e) {
    // Fallback if version.txt not found
    version = 'ClusterAxe-v1.5.1';
}

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
