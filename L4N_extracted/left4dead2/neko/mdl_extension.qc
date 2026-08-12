// 适配l4n的一些qc命令示例，请灵活变通

$NekoModel "Head" {
    studio "my_head_model.gltf"
    // .......
    // $Attachment "eyes" 是眼球中心，不合理的位置会导致追踪不准确
    flex "eye_look_up" // 对应gltf文件里四个方向的形态键名称
    flex "eye_look_down"
    flex "eye_look_左"
    flex "eye_look_right"
    // 新增的flexcontroller需要添加到已有的底部，避免破坏原有的排序导致表情联机同步问题
    flexcontroller eyes range -1 1 l4neyes_updown
    flexcontroller eyes range -1 1 l4neyes_rightleft
    "%eye_look_up" = max(-l4neyes_updown, 0) // flex与flexcontroller绑定
    "%eye_look_down" = max(l4neyes_updown, 0)
    "%eye_look_right" = max(-l4neyes_rightleft, 0)
    "%eye_look_左"      = max(l4neyes_rightleft, 0)

    // l4n的模型选项会显示分组名为l4n的控制器
    flex "my_flex1"
    flexcontroller l4n range 0 1 "形态键控制器1"
    "%my_flex1" = "形态键控制器1"

    flex "my_flex2"
    flexcontroller l4n range 0 1 "形态键控制器2"
    "%my_flex2" = "形态键控制器2"
}

$Model "Head" "my_head_model.smd" {
    // ...
    flexfile "my_head_model.vta" {
        defaultflex frame 0
        // .....
        flex "eye_look_up"      frame 41 // 匹配形态键在vta里的位置
        flex "eye_look_down"    frame 42
        flex "eye_look_left"    frame 43
        flex "eye_look_right"   frame 44

        flex "my_flex1"         frame 45
        flex "my_flex2"         frame 46
    }
    // .....
    flexcontroller eyes range -1 1 l4neyes_updown
    flexcontroller eyes range -1 1 l4neyes_rightleft
    "%eye_look_up"    = max(-l4neyes_updown, 0)
    "%eye_look_down"  = max(l4neyes_updown, 0)
    "%eye_look_right" = max(-l4neyes_rightleft, 0)
    "%eye_look_left"  = max(l4neyes_rightleft, 0)


    flexcontroller l4n range 0 1 "形态键控制器1"
    "%my_flex1" = "形态键控制器1"

    flexcontroller l4n range 0 1 "形态键控制器2"
    "%my_flex2" = "形态键控制器2"
}

// 因为引擎的局限，所有BodyGroup选项的排列组合情况不能超过2147483647
// Model/Body/NekoModel也属于BodyGroup

$BodyGroup "鞋子" {
    studio "高跟鞋.smd"
    blank
}

// 超过两个需要带⭐前缀，以防止和静态表情冲突
$BodyGroup "⭐服装" {
    studio "衣服1.smd"
    studio "衣服2.glb"
    studio "衣服3.gltf"
    blank
    studio "衣服4.fbx"
}

$KeyValues
{
    "left4neko" {
        "name" "mdl的用户友好型名称"
        "name_suffix" "弗兰嘻嘻"
        "player_foot" {
            "body_group" "鞋子"
            "0" "0.0,0.0"
            "1" "-2.1,-60.0"
        }
        "scale" "0.5"
    }
}


$TextureGroup "材质预设"
{ 
	{ "Body"        "Face"          } 
	{ "skin1\Body"  "skin1\Face"    }
    { "Body2"       "Face2"         }
    { ..... }
}
// 1: Body.vmt -> Body.vmt          Face.vmt -> Face.vmt
// 2: Body.vmt -> skin1\Body.vmt    Face.vmt -> skin1\Face.vmt
// 3: Body.vmt -> Body2.vmt         Face.vmt -> Face2.vmt

// 需要使用nekomdl才能支持多$TextureGroup
$TextureGroup "衣服颜色" {...}
$TextureGroup "头发颜色" {...}