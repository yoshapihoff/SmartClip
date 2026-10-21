# План: установщик Inno Setup в релизах SmartClip (21.10.2026)

## Что было сломано (разбор)

Скрипты установщика существовали и были корректны: `installer/windows/smartclip.iss`,
`scripts/build-installer-windows.sh` (коммиты `3c6bdfe`, `dcb6e84`, `f302a25`, `dd929d8`).
Почему в релизах только ZIP — три независимые поломки, каждая «молчаливая»:

1. **CI вообще не звал сборщик установщика.**
   `.forgejo/ci/win-build.sh` на winpc запускал `scripts/release-windows.sh` —
   тот делает только портативный набор + ZIP. `build-installer-windows.sh` не
   вызывался ни разу; workflow забирал с winpc ровно один файл —
   `SmartClip-<ver>-windows-x86_64.zip`.
2. **Шаг сборки падал ещё до Inno:**
   * `OPENSSL_ROOT_DIR` не задавался → `Could NOT find OpenSSL` (CMake abort);
   * дефолт Qt в `build-installer-windows.sh` был `C:/Qt/6.11.2/mingw_64` —
     такого префикса на winpc нет (там 6.8.3).
3. **Сам вызов ISCC не работал в MSYS2.** bash превращает `/DAppVersion=…` в
   путь `D:/…`, ISCC видит «лишний файл скрипта». Симптом обманчивый:
   `You may not specify more than one script filename`, код возврата 0, файла нет.
   Плюс `SDK`-пути считались в подоболочке `$(...)`, где локальные `$STAGE`
   пустые → пустые `//DSourceDir=` → Inno искал файлы относительно `.iss`
   (`..\..\LICENSE` = `<parent>/LICENSE`, а не корень проекта).

## Что сделано

| Файл | Что |
|---|---|
| `.forgejo/ci/win-build.sh` | зовёт `build-installer-windows.sh`, задаёт `OPENSSL_ROOT_DIR=C:/msys64/mingw64`, находит ISCC в `C:/Users/mario/InnoSetup6` |
| `.forgejo/workflows/release.yml` | Windows-джоба забирает и `setup.exe` (отсутствие — warning, не падение); публикует все `*-windows-*` |
| `scripts/build-installer-windows.sh` | Qt-дефолт 6.8.3; пути считаются вне подоболочки; ISCC-флаги через `//D…`; `//DProjectDir=`; суммы один раз в конце, вне каталога |
| `installer/windows/smartclip.iss` | `ProjectDir` через `ExtractFileDir` (без wildcard-проблем), версия из `VERSION.txt` при ручном запуске |
| `scripts/release-windows.sh` | `Compress-Archive -LiteralPath`, суммы через `mktemp` |
| `scripts/release-all.sh` | Windows: `build-installer-windows.sh` + забирает оба артефакта |
| `docs/BUILD-WINDOWS.md` | раздел «5а. Установщик», грабли ISCC/OpenSSL |

## Проверка (live, на winpc)

```
==> Inno Setup: /c/Users/mario/InnoSetup6/ISCC.exe
Successful compile (2,812 sec). Resulting Setup program filename is:
  ...\dist\release-1.1.5\SmartClip-1.1.5-windows-x86_64-setup.exe
SHA256SUMS: setup.exe + zip
```

Забрано на mserver: `setup.exe` 5.2 МБ (Inno, `PE32 GUI`), `zip` 4.0 МБ (16 файлов,
exe + Qt/OpenSSL DLL), суммы сходятся.

## Состояние

Изменения закоммичены локально (`4ba2991`), **не запушены** — по правилу
проекта пуш после прогона Лёши. Проверено вручную на winpc, полный прогон
workflow ещё не запускался.

## Осталось решить

* Release-workflow живёт в Forgejo; GitHub-релизы собираются отдельным
  `.github/workflows/release.yml`, где Windows **не собирается вообще**
  (только Linux + macOS). Если установщик нужен и в GitHub-релизах — надо либо
  добавить туда winpc-джобу (self-hosted раннер или доступ к winpc из
  GitHub-раннера + секрет), либо публиковать Windows-ассеты из Forgejo-релиза
  отдельным шагом.


---

# Дополнение (вторая итерация): где на самом деле терялся установщик в GitHub

Копнул глубже — главная причина **не в CI-скриптах**, а в репозитории:

**В GitHub нет Windows-кода.** `origin/main` = `1ca9bfb` (12.10.2026,
`VERSION` = 1.1.3), merge-base с нашим main — он же. GitHub отстал на ~20
коммитов: Windows-порт (`8857cba`), установщик (`3c6bdfe`), `win-build.sh`,
`installer/`, `icons/SmartClip.ico`, `VERSION.txt` есть только на Forgejo.
GitHub Actions там собирают только Linux + macOS, поэтому Windows-ассеты
докладывались руками.

Сделано:
1. В GitHub-релиз **v1.1.5** добавлен `SmartClip-1.1.5-windows-x86_64-setup.exe`
   (5.2 МБ) и перевыпущен `SHA256SUMS` (проверено скачиванием).
2. `.github/workflows/release.yml` — publish тянет Windows-ассеты из релиза
   Forgejo того же тега. Грабли: Forgejo `/releases/tags/<tag>` → 404 (нужен
   список + фильтр), репа приватная (без токена API 404) → секрет
   **`FORGEJO_READ_TOKEN`**.
3. `tasks/smartclip-publish.py` — GitHub через REST API (на mserver нет `gh`).

Блокеры: PAT без scope `workflow` не пушит workflow-файлы; Windows-код в GitHub
отсутствует (решение о переносе — за Лёшей).
