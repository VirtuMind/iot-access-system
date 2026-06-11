# Projet 10 - Systeme de gestion des acces et presence a domicile

Ce depot contient les fichiers techniques du mini-projet IoT realise avec **ESP32**, **Wokwi** et **Blynk**.

Le rapport final et la presentation PowerPoint restent dans le dossier local voisin `../local-deliverables/` et ne sont pas versionnes dans Git.

## Objectif

Le systeme detecte l'occupation d'un logement et gere les evenements de presence ou de visiteur. Il utilise une fusion de capteurs pour limiter les faux positifs :

```text
Presence confirmee = PIR actif ET distance HC-SR04 < 200 cm
```

L'ESP32 affiche l'etat localement sur un LCD, pilote des LEDs et un buzzer, puis envoie les informations vers Blynk.

## Composants

| Composant | Broche ESP32 | Role |
|---|---:|---|
| PIR motion sensor | GPIO14 | Detection de mouvement zone entree |
| HC-SR04 TRIG | GPIO5 | Impulsion ultrason |
| HC-SR04 ECHO | GPIO18 | Mesure echo distance |
| DHT22 | GPIO4 | Temperature salon |
| Bouton poussoir | GPIO13 | Sonnette simulee |
| LCD I2C SDA | GPIO21 | Affichage local |
| LCD I2C SCL | GPIO22 | Affichage local |
| LED verte | GPIO25 | Logement occupe |
| LED bleue | GPIO26 | Visiteur detecte |
| Buzzer | GPIO12 | Sonnette et alertes |

## Fichiers

```text
wokwi/sketch.ino      Code Arduino ESP32
wokwi/diagram.json    Montage Wokwi
wokwi/libraries.txt   Bibliotheques Wokwi
assets/schema_fonctionnel.svg
```

## Configuration Blynk

Creer les datastreams suivants dans Blynk :

| Pin virtuel | Type conseille | Description |
|---|---|---|
| V0 | String | Etat du logement |
| V1 | Double | Temperature DHT22 |
| V2 | String | Dernier evenement / journal |
| V3 | Integer | Mode vacances |
| V4 | Double | Distance HC-SR04 |
| V5 | Integer | Etat PIR |
| V6 | Double | Heures d'occupation estimees |

Avant d'executer le projet, remplacer dans `wokwi/sketch.ino` :

```cpp
#define BLYNK_TEMPLATE_ID "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "Presence Domicile"
#define BLYNK_AUTH_TOKEN "VotreAuthToken"
```

## Tests principaux

1. PIR seul actif avec distance superieure a 200 cm : presence non confirmee.
2. HC-SR04 seul avec distance inferieure a 200 cm : presence non confirmee.
3. PIR actif et distance inferieure a 200 cm : logement occupe.
4. Aucune detection apres le delai : logement vide.
5. Absence prolongee et temperature inferieure a 10 degC : alerte Blynk.
6. Bouton sonnette : LED bleue, buzzer et notification visiteur.
7. Mode vacances actif avec mouvement : alerte renforcee.

## Notes de simulation

Les delais sont volontairement raccourcis dans le code pour faciliter la demonstration Wokwi. Les constantes indiquent aussi les valeurs reelles attendues par le cahier des charges.
