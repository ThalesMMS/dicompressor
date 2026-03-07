#!/usr/bin/env bash
set -euo pipefail

# Dependências C++ / CLI
brew install openjph gdcm dcmtk

# Ambiente Python
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt

echo
echo "Instalação concluída."
echo "Ative o ambiente: source .venv/bin/activate"
echo "Teste as CLIs: which ojph_compress && which gdcmconv && dcmdump --version"
