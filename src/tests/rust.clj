(def Root (Node))

(schedule
 Root
 (Chain
  "test"
  "Hello world"
  (Hash.Keccak256) = .hash
  (ToHex)
  (Assert.Is "ed6c11b0b5b808960df26f5bfc471d04c1995b0ffd2055925ad1be28d6baadfd" true)
  .hash (Sign.ECDSA "1536f1d756d1abf83aaf173bc5ee3fc487c93010f18624d80bd6d4038fadd59e")
  (Log)))

(run Root)
