document.addEventListener('DOMContentLoaded', () => {
    const searchInput = document.getElementById('search-input');
    const searchResults = document.getElementById('search-results');
    let searchIndex = [];

    // Load search index
    fetch('search_index.json')
        .then(response => response.json())
        .then(data => {
            searchIndex = data;
        })
        .catch(err => {
            console.error('Failed to load search index:', err);
        });

    // Close results when clicking outside
    document.addEventListener('click', (e) => {
        if (!searchInput.contains(e.target) && !searchResults.contains(e.target)) {
            searchResults.style.display = 'none';
        }
    });

    searchInput.addEventListener('input', () => {
        const query = searchInput.value.toLowerCase().strip();
        if (!query) {
            searchResults.style.display = 'none';
            return;
        }

        const matches = searchIndex.filter(item => {
            return item.name.toLowerCase().includes(query) || 
                   item.kind.toLowerCase().includes(query) ||
                   (item.desc && item.desc.toLowerCase().includes(query));
        }).slice(0, 10); // cap at 10 results

        if (matches.length === 0) {
            searchResults.innerHTML = '<div class="search-result-item" style="cursor: default; color: var(--text-muted);">No results found</div>';
        } else {
            searchResults.innerHTML = matches.map(item => `
                <div class="search-result-item">
                    <a href="${item.url}">
                        <div class="search-result-name">${item.name}</div>
                        <div class="search-result-kind">${item.kind}</div>
                    </a>
                </div>
            `).join('');
        }
        searchResults.style.display = 'block';
    });
});

// Helper for python strip equivalents
String.prototype.strip = function() {
    return this.replace(/^\s+|\s+$/g, '');
};
