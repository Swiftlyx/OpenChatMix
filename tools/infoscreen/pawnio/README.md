# PawnIO modules

Signed modules for [PawnIO](https://pawnio.eu/), from the release
[PawnIO.Modules 0.2.11](https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11) by namazso
(`release_0_2_11.zip`, SHA-256 `43608cb89bc84247fef1368a139013f7d043e17db6d6c8dfc9b46bf0905a81f4`), unchanged.
Their source is in that repository at the same tag; they are under the GNU Lesser General Public License 2.1 or
later (`COPYING`).

The Info Screen add-on embeds them as resources and loads them into PawnIO when it is installed and the add-on runs
as administrator:

| File | SHA-256 | Used for |
|---|---|---|
| `AMDFamily17.bin` | `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91` | The temperature of AMD Ryzen processors (families 17h to 1Ah) |
| `IntelMSR.bin` | `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f` | The temperature of Intel processors |
| `LpcIO.bin` | `b3896a1cab0d808fca31fe2ebcae045d59dac690da87b17c858bb8da357eb45e` | The motherboard's fans, from its Super I/O chip |
